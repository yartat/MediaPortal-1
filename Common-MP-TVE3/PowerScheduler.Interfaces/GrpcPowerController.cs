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
using System.Threading;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using P = TvEngine.PowerScheduler.Interfaces.Proto;

namespace TvEngine.PowerScheduler.Interfaces
{
  /// <summary>
  /// <see cref="IPowerController"/> of a remote TV server, implemented on the gRPC PowerControl service.
  /// </summary>
  internal sealed class GrpcPowerController : IPowerController, IDisposable
  {
    private static readonly Empty E = new Empty();
    private static readonly TimeSpan CallTimeout = TimeSpan.FromSeconds(30);

    private readonly Channel _channel;
    private readonly P.PowerControl.PowerControlClient _client;

    private readonly object _sessionLock = new object();
    private CancellationTokenSource _session;

    public GrpcPowerController(Channel channel)
    {
      _channel = channel;
      _client = new P.PowerControl.PowerControlClient(channel);
    }

    public void Dispose()
    {
      StopHandlerSession();
      _channel.ShutdownAsync().Wait(2000);
    }

    private static CallOptions O
    {
      get { return new CallOptions(deadline: DateTime.UtcNow.Add(CallTimeout)); }
    }

    #region IPowerController

    public void SuspendSystem(string source, bool force)
    {
      _client.SuspendSystem(new P.SuspendSystemRequest { Source = source ?? string.Empty, Force = force }, O);
    }

    public void SuspendSystem(string source, int how, bool force)
    {
      _client.SuspendSystem(new P.SuspendSystemRequest { Source = source ?? string.Empty, How = how, Force = force }, O);
    }

    public void SetStandbyAllowed(bool standbyAllowed, string handlerName)
    {
      _client.SetStandbyAllowed(
        new P.SetStandbyAllowedRequest { StandbyAllowed = standbyAllowed, HandlerName = handlerName ?? string.Empty }, O);
    }

    public void SetNextWakeupTime(DateTime nextWakeupTime, string handlerName)
    {
      _client.SetNextWakeupTime(
        new P.SetNextWakeupTimeRequest { NextWakeupTimeTicks = nextWakeupTime.Ticks, HandlerName = handlerName ?? string.Empty },
        O);
    }

    public void UserActivityDetected(DateTime when)
    {
      _client.UserActivityDetected(new Int64Value { Value = when.Ticks }, O);
    }

    public bool IsConnected
    {
      get { return _client.IsConnected(E, O).Value; }
    }

    public void GetCurrentState(bool refresh, out bool unattended, out bool disAllowShutdown,
                                out String disAllowShutdownHandler, out DateTime nextWakeupTime,
                                out String nextWakeupHandler)
    {
      P.CurrentStateReply reply = _client.GetCurrentState(new BoolValue { Value = refresh }, O);
      unattended = reply.Unattended;
      disAllowShutdown = reply.DisallowShutdown;
      disAllowShutdownHandler = reply.DisallowShutdownHandler;
      nextWakeupTime = new DateTime(reply.NextWakeupTimeTicks);
      nextWakeupHandler = reply.NextWakeupHandler;
    }

    public IPowerSettings PowerSettings
    {
      get { return PowerControlProto.FromProto(_client.GetPowerSettings(E, O)); }
    }

    #endregion

    #region handler session

    /// <summary>
    /// Keeps a HandlerSession open (reconnecting if needed) in which the server asks the given handlers.
    /// </summary>
    public void StartHandlerSession(IStandbyHandler standby, IWakeupHandler wakeup, Action<string> log)
    {
      lock (_sessionLock)
      {
        if (_session != null)
          return;
        _session = new CancellationTokenSource();
        CancellationToken token = _session.Token;
        new Thread(() => RunHandlerSession(standby, wakeup, log, token)) { IsBackground = true, Name = "PS handler session" }
          .Start();
      }
    }

    public void StopHandlerSession()
    {
      lock (_sessionLock)
      {
        if (_session != null)
        {
          _session.Cancel();
          _session = null;
        }
      }
    }

    private void RunHandlerSession(IStandbyHandler standby, IWakeupHandler wakeup, Action<string> log,
                                   CancellationToken token)
    {
      while (!token.IsCancellationRequested)
      {
        try
        {
          using (AsyncDuplexStreamingCall<P.HandlerAnswer, P.HandlerQuery> call =
            _client.HandlerSession(new CallOptions(cancellationToken: token)))
          {
            if (log != null)
              log("PS: handler session with server's PowerScheduler established");
            while (call.ResponseStream.MoveNext(token).GetAwaiter().GetResult())
            {
              P.HandlerQuery query = call.ResponseStream.Current;
              call.RequestStream.WriteAsync(Answer(query, standby, wakeup)).Wait(token);
            }
          }
        }
        catch (Exception ex)
        {
          if (token.IsCancellationRequested)
            return;
          if (log != null)
            log("PS: handler session interrupted, retrying - " + ex.Message);
        }
        token.WaitHandle.WaitOne(5000);
      }
    }

    private static P.HandlerAnswer Answer(P.HandlerQuery query, IStandbyHandler standby, IWakeupHandler wakeup)
    {
      var answer = new P.HandlerAnswer { Id = query.Id };
      switch (query.Kind)
      {
        case P.HandlerQueryKind.HandlerQueryDisallowShutdown:
          answer.DisallowShutdown = standby.DisAllowShutdown;
          break;
        case P.HandlerQueryKind.HandlerQueryUserShutdownNow:
          standby.UserShutdownNow();
          break;
        case P.HandlerQueryKind.HandlerQueryStandbyHandlerName:
          answer.HandlerName = standby.HandlerName ?? string.Empty;
          break;
        case P.HandlerQueryKind.HandlerQueryNextWakeupTime:
          answer.NextWakeupTimeTicks = wakeup.GetNextWakeupTime(new DateTime(query.EarliestWakeupTimeTicks)).Ticks;
          break;
        case P.HandlerQueryKind.HandlerQueryWakeupHandlerName:
          answer.HandlerName = wakeup.HandlerName ?? string.Empty;
          break;
      }
      return answer;
    }

    #endregion
  }
}
