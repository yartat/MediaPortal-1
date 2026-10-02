#region Copyright (C) 2005-2026 Team MediaPortal

// Copyright (C) 2005-2026 Team MediaPortal
// http://www.team-mediaportal.com
//
// MediaPortal is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// MediaPortal is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with MediaPortal. If not, see <http://www.gnu.org/licenses/>.

#endregion

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using TvEngine.PowerScheduler.Interfaces;
using TvLibrary.Log;
using P = TvEngine.PowerScheduler.Interfaces.Proto;

[assembly: System.Runtime.CompilerServices.InternalsVisibleTo("TvControlGrpcTests")]

namespace TvEngine.PowerScheduler
{
  /// <summary>
  /// Registry for the standby/wakeup handlers of client handler sessions (implemented by <see cref="PowerScheduler"/>).
  /// </summary>
  internal interface IClientSessionRegistry
  {
    void RegisterClientSession(IStandbyHandler standbyHandler, IWakeupHandler wakeupHandler);
    void UnregisterClientSession(IStandbyHandler standbyHandler, IWakeupHandler wakeupHandler);
  }

  /// <summary>
  /// gRPC PowerControl service: forwards to the server's <see cref="PowerScheduler"/>.
  /// </summary>
  internal sealed class PowerControlGrpcService : P.PowerControl.PowerControlBase
  {
    private static readonly Empty E = new Empty();
    private readonly IPowerController _ps;
    private readonly IClientSessionRegistry _sessions;

    public PowerControlGrpcService(IPowerController powerController, IClientSessionRegistry sessions)
    {
      _ps = powerController;
      _sessions = sessions;
    }

    public override Task<Empty> SuspendSystem(P.SuspendSystemRequest request, ServerCallContext context)
    {
      if (request.HasHow)
        _ps.SuspendSystem(request.Source, request.How, request.Force);
      else
        _ps.SuspendSystem(request.Source, request.Force);
      return Task.FromResult(E);
    }

    public override Task<Empty> SetStandbyAllowed(P.SetStandbyAllowedRequest request, ServerCallContext context)
    {
      _ps.SetStandbyAllowed(request.StandbyAllowed, request.HandlerName);
      return Task.FromResult(E);
    }

    public override Task<Empty> SetNextWakeupTime(P.SetNextWakeupTimeRequest request, ServerCallContext context)
    {
      _ps.SetNextWakeupTime(new DateTime(request.NextWakeupTimeTicks), request.HandlerName);
      return Task.FromResult(E);
    }

    public override Task<Empty> UserActivityDetected(Int64Value request, ServerCallContext context)
    {
      _ps.UserActivityDetected(new DateTime(request.Value));
      return Task.FromResult(E);
    }

    public override Task<BoolValue> IsConnected(Empty request, ServerCallContext context)
    {
      return Task.FromResult(new BoolValue { Value = _ps.IsConnected });
    }

    public override Task<P.CurrentStateReply> GetCurrentState(BoolValue request, ServerCallContext context)
    {
      bool unattended, disAllowShutdown;
      string disAllowShutdownHandler, nextWakeupHandler;
      DateTime nextWakeupTime;
      _ps.GetCurrentState(request.Value, out unattended, out disAllowShutdown, out disAllowShutdownHandler,
                          out nextWakeupTime, out nextWakeupHandler);
      return Task.FromResult(new P.CurrentStateReply
                               {
                                 Unattended = unattended,
                                 DisallowShutdown = disAllowShutdown,
                                 DisallowShutdownHandler = disAllowShutdownHandler ?? string.Empty,
                                 NextWakeupTimeTicks = nextWakeupTime.Ticks,
                                 NextWakeupHandler = nextWakeupHandler ?? string.Empty
                               });
    }

    public override Task<P.PowerSettings> GetPowerSettings(Empty request, ServerCallContext context)
    {
      return Task.FromResult(PowerControlProto.ToProto(_ps.PowerSettings));
    }

    /// <summary>
    /// While the client keeps this call open, its PowerScheduler is registered as standby and wakeup handler.
    /// </summary>
    public override async Task HandlerSession(IAsyncStreamReader<P.HandlerAnswer> requestStream,
                                              IServerStreamWriter<P.HandlerQuery> responseStream,
                                              ServerCallContext context)
    {
      var session = new ClientHandlerSession(responseStream);
      var standby = new SessionStandbyHandler(session);
      var wakeup = new SessionWakeupHandler(session);
      Log.Debug("PS: client handler session {0} opened by {1}", session.Tag, context.Peer);
      _sessions.RegisterClientSession(standby, wakeup);
      try
      {
        while (await requestStream.MoveNext(context.CancellationToken).ConfigureAwait(false))
          session.OnAnswer(requestStream.Current);
      }
      catch (Exception ex)
      {
        Log.Debug("PS: client handler session {0} ended - {1}", session.Tag, ex.Message);
      }
      finally
      {
        session.Close();
        _sessions.UnregisterClientSession(standby, wakeup);
        Log.Debug("PS: client handler session {0} closed", session.Tag);
      }
    }
  }

  /// <summary>
  /// Server side of a client's handler session: sends a query and waits for the matching answer.
  /// </summary>
  internal sealed class ClientHandlerSession
  {
    private static readonly TimeSpan AnswerTimeout = TimeSpan.FromSeconds(5);
    private static int _nextTag;

    private readonly IServerStreamWriter<P.HandlerQuery> _writer;
    private readonly SemaphoreSlim _writeLock = new SemaphoreSlim(1, 1);
    private readonly ConcurrentDictionary<long, TaskCompletionSource<P.HandlerAnswer>> _pending =
      new ConcurrentDictionary<long, TaskCompletionSource<P.HandlerAnswer>>();
    private long _nextId;
    private volatile bool _closed;

    public ClientHandlerSession(IServerStreamWriter<P.HandlerQuery> writer)
    {
      _writer = writer;
      Tag = Interlocked.Increment(ref _nextTag);
    }

    public int Tag { get; private set; }

    /// <summary>Returns null if the client is gone or did not answer in time.</summary>
    public P.HandlerAnswer Ask(P.HandlerQueryKind kind, long earliestWakeupTimeTicks = 0)
    {
      if (_closed)
        return null;
      long id = Interlocked.Increment(ref _nextId);
      var answer = new TaskCompletionSource<P.HandlerAnswer>(TaskCreationOptions.RunContinuationsAsynchronously);
      _pending[id] = answer;
      try
      {
        if (!_writeLock.Wait(AnswerTimeout))
          return null;
        try
        {
          if (!_writer.WriteAsync(new P.HandlerQuery { Id = id, Kind = kind, EarliestWakeupTimeTicks = earliestWakeupTimeTicks })
                      .Wait(AnswerTimeout))
            return null;
        }
        finally
        {
          _writeLock.Release();
        }
        return answer.Task.Wait(AnswerTimeout) ? answer.Task.Result : null;
      }
      catch (Exception ex)
      {
        Log.Debug("PS: client handler session {0}: {1}", Tag, ex.Message);
        return null;
      }
      finally
      {
        TaskCompletionSource<P.HandlerAnswer> ignored;
        _pending.TryRemove(id, out ignored);
      }
    }

    public void OnAnswer(P.HandlerAnswer answer)
    {
      TaskCompletionSource<P.HandlerAnswer> pending;
      if (_pending.TryRemove(answer.Id, out pending))
        pending.TrySetResult(answer);
    }

    public void Close()
    {
      _closed = true;
      foreach (TaskCompletionSource<P.HandlerAnswer> pending in _pending.Values)
        pending.TrySetResult(null);
    }
  }

  /// <summary>
  /// Standby handler backed by a client's handler session (formerly LocalClientStandbyHandler).
  /// </summary>
  internal sealed class SessionStandbyHandler : IStandbyHandler
  {
    private readonly ClientHandlerSession _session;

    public SessionStandbyHandler(ClientHandlerSession session)
    {
      _session = session;
    }

    public bool DisAllowShutdown
    {
      get
      {
        P.HandlerAnswer answer = _session.Ask(P.HandlerQueryKind.HandlerQueryDisallowShutdown);
        return answer != null && answer.DisallowShutdown;
      }
    }

    public void UserShutdownNow()
    {
      _session.Ask(P.HandlerQueryKind.HandlerQueryUserShutdownNow);
    }

    public string HandlerName
    {
      get
      {
        P.HandlerAnswer answer = _session.Ask(P.HandlerQueryKind.HandlerQueryStandbyHandlerName);
        return answer != null ? answer.HandlerName : "<dead#" + _session.Tag + ">";
      }
    }
  }

  /// <summary>
  /// Wakeup handler backed by a client's handler session (formerly LocalClientWakeupHandler).
  /// </summary>
  internal sealed class SessionWakeupHandler : IWakeupHandler
  {
    private readonly ClientHandlerSession _session;

    public SessionWakeupHandler(ClientHandlerSession session)
    {
      _session = session;
    }

    public DateTime GetNextWakeupTime(DateTime earliestWakeupTime)
    {
      P.HandlerAnswer answer = _session.Ask(P.HandlerQueryKind.HandlerQueryNextWakeupTime, earliestWakeupTime.Ticks);
      return answer != null ? new DateTime(answer.NextWakeupTimeTicks) : DateTime.MaxValue;
    }

    public string HandlerName
    {
      get
      {
        P.HandlerAnswer answer = _session.Ask(P.HandlerQueryKind.HandlerQueryWakeupHandlerName);
        return answer != null ? answer.HandlerName : "<dead#" + _session.Tag + ">";
      }
    }
  }
}
