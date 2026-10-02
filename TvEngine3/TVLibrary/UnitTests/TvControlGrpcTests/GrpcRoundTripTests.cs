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
using System.Security.Authentication;
using System.Threading;
using DirectShowLib.BDA;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using NUnit.Framework;
using TvControl;
using MediaPortal.RemoteApi;
using TvLibrary;
using TvLibrary.Channels;
using TvLibrary.Implementations;
using TvLibrary.Interfaces;
using TvService.GrpcApi;
using P = TvControl.Grpc.Proto;

namespace TvControlGrpcTests
{
  /// <summary>
  /// End-to-end: real gRPC server (TLS + API key) on the TvControl port, client through RemoteControl.Instance.
  /// </summary>
  [TestFixture, NonParallelizable]
  public class GrpcRoundTripTests
  {
    private const string Host = "127.0.0.1";
    private FakeProxy<IController> _fake;
    private GrpcServerHost _host;
    private ServerKeyMaterial _material;

    [OneTimeSetUp]
    public void StartServer()
    {
      _material = GrpcSecurity.CreateServerMaterial();
      _fake = new FakeProxy<IController>();
      _host = new GrpcServerHost();
      _host.Start(_fake.Instance, _material);

      RemoteControl.HostName = Host;
      RemoteControl.ConnectionKey = _material.ConnectionKey.ToString();
    }

    [OneTimeTearDown]
    public void StopServer()
    {
      RemoteControl.Clear();
      RemoteControl.ConnectionKey = null;
      _host.Stop();
    }

    [Test]
    public void SimpleCallIsForwarded()
    {
      _fake.Handlers["get_Cards/0"] = args => 3;
      Assert.That(RemoteControl.Instance.Cards, Is.EqualTo(3));
    }

    [Test]
    public void RefAndOutParametersRoundTrip()
    {
      int requestedChannel = -1;
      _fake.Handlers["StartTimeShifting/3"] = args =>
        {
          var u = (IUser)args[0];
          requestedChannel = (int)args[1];
          u.CardId = 7;
          u.Priority = 4;
          args[2] = new VirtualCard(u, "server") { RecordingFolder = @"D:\rec" };
          return TvResult.Succeeded;
        };

      IUser user = new User("tester", false);
      VirtualCard card;
      TvResult result = RemoteControl.Instance.StartTimeShifting(ref user, 42, out card);

      Assert.That(result, Is.EqualTo(TvResult.Succeeded));
      Assert.That(requestedChannel, Is.EqualTo(42));
      Assert.That(user.Name, Is.EqualTo("tester"));
      Assert.That(user.CardId, Is.EqualTo(7));
      Assert.That(user.Priority, Is.EqualTo(4));
      Assert.That(card.Id, Is.EqualTo(7));
      Assert.That(card.RemoteServer, Is.EqualTo("server"));
      Assert.That(card.RecordingFolder, Is.EqualTo(@"D:\rec"));
    }

    [Test]
    public void PolymorphicChannelsRoundTrip()
    {
      IChannel received = null;
      _fake.Handlers["Scan/2"] = args =>
        {
          received = (IChannel)args[1];
          return new IChannel[]
                   {
                     new DVBSChannel { Name = "Sat", Polarisation = Polarisation.LinearV, SymbolRate = 27500, ServiceId = 17 },
                     new AnalogChannel
                       {
                         Name = "Analog",
                         ChannelNumber = 5,
                         Country = new Country(31, "Netherlands", "NL", DirectShowLib.AnalogVideoStandard.PAL_B)
                       }
                   };
        };

      IChannel[] channels = RemoteControl.Instance.Scan(1, new DVBTChannel { Frequency = 506000, BandWidth = 8 });

      Assert.That(received, Is.TypeOf<DVBTChannel>());
      Assert.That(((DVBTChannel)received).BandWidth, Is.EqualTo(8));
      Assert.That(((DVBTChannel)received).Frequency, Is.EqualTo(506000));
      Assert.That(channels, Has.Length.EqualTo(2));
      var sat = (DVBSChannel)channels[0];
      Assert.That(sat.Polarisation, Is.EqualTo(Polarisation.LinearV));
      Assert.That(sat.SymbolRate, Is.EqualTo(27500));
      Assert.That(sat.ServiceId, Is.EqualTo(17));
      var analog = (AnalogChannel)channels[1];
      Assert.That(analog.ChannelNumber, Is.EqualTo(5));
      Assert.That(analog.Country.Code, Is.EqualTo("NL"));
    }

    [Test]
    public void NullStringsStayNull()
    {
      _fake.Handlers["CardName/1"] = args => null;
      Assert.That(RemoteControl.Instance.CardName(1), Is.Null);
    }

    [Test]
    public void CiMenuIsStreamedToClient()
    {
      CiMenu received = null;
      var gotIt = new ManualResetEventSlim();
      CiMenuCallback handler = menu =>
        {
          received = menu;
          gotIt.Set();
        };
      RemoteControl.Instance.OnCiMenu += handler;
      try
      {
        var menu = new CiMenu("Title", "Sub", "Bottom", CiMenuState.Ready) { NumChoices = 1 };
        menu.AddEntry(1, "Entry");
        // The stream is set up asynchronously: keep firing until the client got it.
        for (int i = 0; i < 50 && !gotIt.IsSet; i++)
        {
          ((CiMenuCallback)_fake.Subscribers["OnCiMenu"])(menu);
          gotIt.Wait(200);
        }
      }
      finally
      {
        RemoteControl.Instance.OnCiMenu -= handler;
      }

      Assert.That(received, Is.Not.Null);
      Assert.That(received.Title, Is.EqualTo("Title"));
      Assert.That(received.State, Is.EqualTo(CiMenuState.Ready));
      Assert.That(received.MenuEntries[0].Message, Is.EqualTo("Entry"));
    }

    [Test]
    public void CallsWithoutValidApiKeyAreRejected()
    {
      foreach (string key in new[] { null, "wrong" })
      {
        var credentials = key == null
                            ? (ChannelCredentials)new SslCredentials(_material.CaCertificatePem)
                            : ChannelCredentials.Create(new SslCredentials(_material.CaCertificatePem),
                                                        CallCredentials.FromInterceptor((c, m) =>
                                                          {
                                                            m.Add(GrpcSecurity.AuthorizationHeader, GrpcSecurity.BearerPrefix + key);
                                                            return System.Threading.Tasks.Task.CompletedTask;
                                                          }));
        var channel = new Channel(Host, RemoteControl.Port, credentials,
                                  new[] { new ChannelOption(ChannelOptions.SslTargetNameOverride, GrpcSecurity.TargetNameOverride) });
        try
        {
          var client = new P.TvControlService.TvControlServiceClient(channel);
          var ex = Assert.Throws<RpcException>((TestDelegate)(() => client.GetCards(new Empty(), deadline: DateTime.UtcNow.AddSeconds(10))));
          Assert.That(ex.StatusCode, Is.EqualTo(StatusCode.Unauthenticated));
        }
        finally
        {
          channel.ShutdownAsync().Wait();
        }
      }
    }

    [Test]
    public void ServerCertificateMustMatchPinnedFingerprint()
    {
      Assert.That(GrpcSecurity.FetchPinnedCaCertificatePem(Host, RemoteControl.Port, _material.CaFingerprint, 5000),
                  Is.EqualTo(_material.CaCertificatePem));
      Assert.Throws<AuthenticationException>(
        (TestDelegate)(() => GrpcSecurity.FetchPinnedCaCertificatePem(Host, RemoteControl.Port, new string('0', 64), 5000)));
    }

    [Test]
    public void ConnectionKeyTextRoundTrip()
    {
      ConnectionKey parsed;
      Assert.That(ConnectionKey.TryParse(_material.ConnectionKey.ToString(), out parsed), Is.True);
      Assert.That(parsed.ApiKey, Is.EqualTo(_material.ApiKey));
      Assert.That(parsed.CaFingerprint, Is.EqualTo(_material.CaFingerprint));
      Assert.That(ConnectionKey.TryParse("mptv1.short.abc", out parsed), Is.False);
    }
  }
}
