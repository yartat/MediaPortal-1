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
using System.Diagnostics;
using System.Threading;
using Grpc.Core;
using MediaPortal.RemoteApi;
using NUnit.Framework;
using TvEngine.PowerScheduler;
using TvEngine.PowerScheduler.Interfaces;
using P = TvEngine.PowerScheduler.Interfaces.Proto;

namespace TvControlGrpcTests
{
  /// <summary>
  /// End-to-end: PowerControl gRPC service of the server plugin, client through RemotePowerControl.
  /// </summary>
  [TestFixture, NonParallelizable]
  public class PowerControlSessionTests
  {
    private sealed class Registry : IClientSessionRegistry
    {
      public volatile IStandbyHandler Standby;
      public volatile IWakeupHandler Wakeup;

      public void RegisterClientSession(IStandbyHandler standbyHandler, IWakeupHandler wakeupHandler)
      {
        Standby = standbyHandler;
        Wakeup = wakeupHandler;
      }

      public void UnregisterClientSession(IStandbyHandler standbyHandler, IWakeupHandler wakeupHandler)
      {
        Standby = null;
        Wakeup = null;
      }
    }

    private sealed class ClientHandlers : IStandbyHandler, IWakeupHandler
    {
      public volatile bool ShutdownRequested;

      public bool DisAllowShutdown
      {
        get { return true; }
      }

      public void UserShutdownNow()
      {
        ShutdownRequested = true;
      }

      string IStandbyHandler.HandlerName
      {
        get { return "client-standby"; }
      }

      public DateTime GetNextWakeupTime(DateTime earliestWakeupTime)
      {
        return earliestWakeupTime.AddHours(1);
      }

      string IWakeupHandler.HandlerName
      {
        get { return "client-wakeup"; }
      }
    }

    private FakeProxy<IPowerController> _fake;
    private Registry _registry;
    private Server _server;

    [OneTimeSetUp]
    public void StartServer()
    {
      ServerKeyMaterial material = GrpcSecurity.CreateServerMaterial();
      _fake = new FakeProxy<IPowerController>();
      _registry = new Registry();
      _server = SecureGrpcServer.Create(RemotePowerControl.Port, material, null,
                                        P.PowerControl.BindService(new PowerControlGrpcService(_fake.Instance, _registry)));
      _server.Start();
      RemotePowerControl.HostName = "127.0.0.1";
      RemotePowerControl.ConnectionKey = material.ConnectionKey.ToString();
    }

    [OneTimeTearDown]
    public void StopServer()
    {
      RemotePowerControl.StopHandlerSession();
      RemotePowerControl.Clear();
      _server.ShutdownAsync().Wait();
    }

    private static bool WaitFor(Func<bool> condition)
    {
      Stopwatch watch = Stopwatch.StartNew();
      while (!condition() && watch.ElapsedMilliseconds < 15000)
        Thread.Sleep(50);
      return condition();
    }

    [Test]
    public void UnaryCallsRoundTrip()
    {
      DateTime received = DateTime.MinValue;
      _fake.Handlers["UserActivityDetected/1"] = args =>
        {
          received = (DateTime)args[0];
          return null;
        };
      _fake.Handlers["get_PowerSettings/0"] = args => new PowerSettings
                                                         {
                                                           IdleTimeout = 42,
                                                           ShutdownMode = ShutdownMode.Hibernate,
                                                           AllowedSleepStartTime = 3
                                                         };

      var when = new DateTime(2026, 10, 2, 20, 15, 0);
      RemotePowerControl.Instance.UserActivityDetected(when);
      IPowerSettings settings = RemotePowerControl.Instance.PowerSettings;

      Assert.That(received, Is.EqualTo(when));
      Assert.That(settings.IdleTimeout, Is.EqualTo(42));
      Assert.That(settings.ShutdownMode, Is.EqualTo(ShutdownMode.Hibernate));
      Assert.That(((PowerSettings)settings).AllowedSleepStartTime, Is.EqualTo(3));
    }

    [Test]
    public void ServerQueriesClientHandlersOverSession()
    {
      var client = new ClientHandlers();
      RemotePowerControl.StartHandlerSession(client, client);
      Assert.That(WaitFor(() => _registry.Standby != null), "client session was not registered");

      IStandbyHandler standby = _registry.Standby;
      IWakeupHandler wakeup = _registry.Wakeup;
      var earliest = new DateTime(2026, 10, 3, 6, 0, 0);
      Assert.That(standby.DisAllowShutdown, Is.True);
      Assert.That(standby.HandlerName, Is.EqualTo("client-standby"));
      Assert.That(wakeup.HandlerName, Is.EqualTo("client-wakeup"));
      Assert.That(wakeup.GetNextWakeupTime(earliest), Is.EqualTo(earliest.AddHours(1)));
      standby.UserShutdownNow();
      Assert.That(client.ShutdownRequested, Is.True);

      RemotePowerControl.StopHandlerSession();
      Assert.That(WaitFor(() => _registry.Standby == null), "client session was not unregistered");
      // A handler of a closed session behaves like a dead remote handler
      Assert.That(standby.DisAllowShutdown, Is.False);
      Assert.That(standby.HandlerName, Does.StartWith("<dead#"));
      Assert.That(wakeup.GetNextWakeupTime(earliest), Is.EqualTo(DateTime.MaxValue));
    }
  }
}
