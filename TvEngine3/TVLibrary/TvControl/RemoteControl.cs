#region Copyright (C) 2005-2021 Team MediaPortal

// Copyright (C) 2005-2021 Team MediaPortal
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
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Linq;
using Grpc.Core;
using MediaPortal.RemoteApi;
using TvControl.Grpc;
using PairingKey = MediaPortal.RemoteApi.ConnectionKey;
using TvLibrary.Interfaces;
using TvLibrary.Log;

namespace TvControl
{
  /// <summary>
  /// Class which holds the connection with the master tv-server (gRPC over TLS, authenticated with an API key)
  /// </summary>
  public class RemoteControl
  {
    #region consts

    public const string InitializedEventName = @"Global\MPTVServiceInitializedEvent";

    private const int MAX_WAIT_FOR_SERVER_REMOTING_CONNECTION = 3000; //msecs
    private const int MAX_WAIT_FOR_SERVER_REMOTING_CONNECTION_INITIAL = 20000; //msecs
    private const int MAX_WAIT_FOR_CHANNEL_REGISTRATION = 3600000; //msecs - 1 hour - Almost infinity, but will keep the server from freezing up.
    private const int MAX_WAIT_FOR_CHANNEL_REGISTRATION_MEDIAPORTAL = 10000; //msecs
    private const int MAX_TCP_TIMEOUT = 1000; //msecs

    /// <summary>TCP port of the TvControl gRPC endpoint (the former remoting port).</summary>
    public const int Port = 31456;

    #endregion

    #region delegates / events

    public delegate void RemotingDisconnectedDelegate();

    public delegate void RemotingConnectedDelegate();

    public static event RemotingDisconnectedDelegate OnRemotingDisconnected;
    public static event RemotingConnectedDelegate OnRemotingConnected;

    #endregion

    #region private members

    private static bool _firstFailure = true;
    private static bool _isRemotingConnected = false;
    private static GrpcControllerClient _tvControl;
    private static IController _tvControlLocal = null;
    private static string _hostName = System.Net.Dns.GetHostName();
    private static PairingKey _connectionKey;
    private static bool _useIncreasedTimeoutForInitialConnection = true;
    private static int _timeout = 0;
    private static object _Padlock = new object();

    #endregion

    #region public constructors

    static RemoteControl()
    {
      _useIncreasedTimeoutForInitialConnection = true;
    }

    #endregion

    #region public static methods

    /// <summary>
    /// Registers Ci Menu Callbackhandler in TvPlugin, connects to a server side event
    /// </summary>
    public static void RegisterCiMenuCallbacks(CiMenuCallbackSink sink)
    {
      RefreshRemotingConnectionStatus();
      IController controller = Instance;
      if (controller != null)
        controller.OnCiMenu += sink.FireCiMenuCallback;
    }

    /// <summary>
    /// Unregisters Ci Menu Callbackhandler in TvPlugin when it's no longer required
    /// </summary>
    public static void UnRegisterCiMenuCallbacks(CiMenuCallbackSink sink)
    {
      IController controller = Instance;
      if (controller != null)
        controller.OnCiMenu -= sink.FireCiMenuCallback;
    }

    /// <summary>
    /// Clears this instance.
    /// </summary>
    public static void Clear()
    {
      lock (_Padlock)
      {
        DisposeClient();
        _isRemotingConnected = false;
      }
    }

    #endregion

    #region private static methods

    private static void DisposeClient()
    {
      GrpcControllerClient client = _tvControl;
      _tvControl = null;
      if (client != null)
      {
        try
        {
          client.Dispose();
        }
        catch (Exception ex)
        {
          Log.Debug("RemoteControl: error closing connection - {0}", ex.Message);
        }
      }
    }

    [MethodImpl(MethodImplOptions.Synchronized)]
    private static void RefreshRemotingConnectionStatus()
    {
      Stopwatch benchClock = Stopwatch.StartNew();
      try
      {
        if (_tvControl == null)
        {
          _isRemotingConnected = false;
          return;
        }

        int timeout = 250;
        if (!_isRemotingConnected && _firstFailure)
        {
          timeout = MAX_WAIT_FOR_SERVER_REMOTING_CONNECTION;
          if (_useIncreasedTimeoutForInitialConnection)
          {
            timeout = MAX_WAIT_FOR_SERVER_REMOTING_CONNECTION_INITIAL;
            _useIncreasedTimeoutForInitialConnection = false;
          }
        }

        int iterations = (timeout / MAX_TCP_TIMEOUT);

        if (iterations < 1)
        {
          iterations = 1;
        }

        int count = 0;

        while (!_isRemotingConnected && count < iterations)
        {
          CheckTcpPort();

          count++;
          if (!_isRemotingConnected)
          {
            Thread.Sleep(10);
          }
        }
      }
      catch (System.Threading.ThreadAbortException)
      {
        benchClock.Stop();
        Log.Info("RemoteControl - timed out after {0} msec", benchClock.ElapsedMilliseconds);
        Thread.ResetAbort();
      }
      catch (Exception e)
      {
        Log.Info("RemoteControl - RefreshRemotingConnectionStatus exception : {0} {1} {2}", e.Message, e.Source,
                 e.StackTrace);
        //ignore
      }
      finally
      {
        InvokeEvents();
      }
    }

    private static void InvokeEvents()
    {
      if (!_isRemotingConnected)
      {
        if (OnRemotingDisconnected != null) //raise event
        {
          if (!_firstFailure)
          {
            Log.Info("RemoteControl - Disconnected ");
            OnRemotingDisconnected();
          }
          _firstFailure = false;
        }
      }
      else
      {
        _firstFailure = true;
        if (OnRemotingConnected != null)
        {
          Log.Info("RemoteControl - Connected");
          OnRemotingConnected();
        }
      }
    }

    private static bool IsHostNameMatch(string strHostNameA, string strHostNameB)
    {
      if (strHostNameA.Equals(strHostNameB, StringComparison.OrdinalIgnoreCase))
        return true;

      IPAddress.TryParse(strHostNameA, out IPAddress ipHostA);
      IPAddress.TryParse(strHostNameB, out IPAddress ipHostB);

      if (ipHostA != null && ipHostB == null)
      {
        IPAddress[] ips = Dns.GetHostAddresses(strHostNameB);
        if (ips != null && ips.Any(ip => ip.Equals(ipHostA)))
          return true;
      }

      if (ipHostA == null && ipHostB != null)
      {
        IPAddress[] ips = Dns.GetHostAddresses(strHostNameA);
        if (ips != null && ips.Any(ip => ip.Equals(ipHostB)))
          return true;
      }

      return false;
    }

    /// <summary>
    /// Creates the authenticated TLS channel to the server.
    /// </summary>
    private static GrpcControllerClient CreateClient(string hostName)
    {
      Channel channel = GrpcClientFactory.CreateChannel(hostName, Port, _connectionKey, MAX_WAIT_FOR_SERVER_REMOTING_CONNECTION);
      return new GrpcControllerClient(channel, () => TimeOut);
    }

    #endregion

    #region public properties

    public static bool IsConnected
    {
      get { return CheckTcpPort(); }
    }

    public static bool UseIncreasedTimeoutForInitialConnection
    {
      get { return _useIncreasedTimeoutForInitialConnection; }
      set
      {
        _useIncreasedTimeoutForInitialConnection = value;
        if (value)
          _firstFailure = true;
      }
    }

    /// <summary>
    /// Gets or sets the name the hostname of the master tv-server.
    /// </summary>
    /// <value>The name of the host.</value>
    public static string HostName
    {
      get
      {
        lock (_Padlock)
        {
          return _hostName;
        }
      }
      set
      {
        lock (_Padlock)
        {
          if (string.IsNullOrWhiteSpace(value))
          {
            Log.Error("RemoteControl: HostName: value is empty.");
            return;
          }

          if (value.Equals(_hostName, StringComparison.OrdinalIgnoreCase))
            return;

          if (IsHostNameMatch(value, _hostName))
          {
            Log.Debug("RemoteControl: HostName: No change: '{0}' == '{1}'", _hostName, value);
            _hostName = value;
            return;
          }

          Log.Debug("RemoteControl: HostName: Change: '{0}' --> '{1}'", _hostName, value);
          DisposeClient();
          _hostName = value;
        }
      }
    }

    /// <summary>
    /// Connection key of the tv-server (API key + pinned CA fingerprint, see <see cref="MediaPortal.RemoteApi.ConnectionKey"/>).
    /// When not set, the key of a tv-server on this machine is used if the process may read it.
    /// </summary>
    public static string ConnectionKey
    {
      get
      {
        lock (_Padlock)
        {
          return _connectionKey == null ? null : _connectionKey.ToString();
        }
      }
      set
      {
        lock (_Padlock)
        {
          PairingKey key = null;
          if (!string.IsNullOrWhiteSpace(value) && !PairingKey.TryParse(value, out key))
          {
            Log.Error("RemoteControl: ConnectionKey: invalid connection key format.");
            return;
          }
          if (key == null ? _connectionKey == null : _connectionKey != null && key.ToString() == _connectionKey.ToString())
            return;
          DisposeClient();
          _connectionKey = key;
        }
      }
    }

    /// <summary>
    /// Gets or sets the per call timeout.
    /// </summary>
    /// <value>Timeout in ms.</value>
    public static int TimeOut
    {
      get
      {
        if (_timeout == 0)
        {
          if (AppDomain.CurrentDomain.FriendlyName.ToLowerInvariant() == "mediaportal.exe")
          {
            return MAX_WAIT_FOR_CHANNEL_REGISTRATION_MEDIAPORTAL;
          }
          return MAX_WAIT_FOR_CHANNEL_REGISTRATION;
        }
        return _timeout;
      }
      set
      {
        _timeout = value;
      }
    }

    public static IPAddress GetIP4Addresses()
    {
      IPAddress ipv4Address = null;

      foreach (IPAddress currentIPAddress in Dns.GetHostAddresses(_hostName))
      {
        if (currentIPAddress.AddressFamily == AddressFamily.InterNetwork)
        {
          ipv4Address = currentIPAddress;
          break;
        }
      }

      return ipv4Address;
    }

    private static bool CheckTcpPort()
    {
      Stopwatch benchClock = Stopwatch.StartNew();
      if (string.IsNullOrEmpty(_hostName))
        return false;

      _isRemotingConnected = false;
      Socket sock = null;

      try
      {
        sock = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
        IPAddress ip = (IPAddress) GetIP4Addresses();
        if (sock != null)
        {
          IPEndPoint ipEndPoint = new IPEndPoint(ip, Port);

          _isRemotingConnected = false;
          //time out MAX_TCP_TIMEOUT
          CallWithTimeout(ConnectToProxyServers, MAX_TCP_TIMEOUT, sock, ipEndPoint);

          if (sock != null && sock.Connected)
          {
            _isRemotingConnected = true;
          }
        }
      }
      catch (Exception)
      {
        _isRemotingConnected = false;
      }
      finally
      {
        benchClock.Stop();
        if (sock != null && sock.Connected)
        {
          Log.Debug("RemoteControl: TCP connect took : {0}", benchClock.ElapsedMilliseconds);
        }
        try
        {
          if (sock != null)
          {
            sock.Shutdown(SocketShutdown.Both);
          }
        }
        catch (Exception)
        {
        }
        finally
        {
          if (sock != null)
            sock.Close();
        }
      }
      return _isRemotingConnected;
    }

    private static void CallWithTimeout(Action<Socket, IPEndPoint> action, int timeoutMilliseconds, Socket socket,
                                 IPEndPoint ipendPoint)
    {
      try
      {
        Action wrappedAction = () =>
                                 {
                                   action(socket, ipendPoint);
                                 };

        IAsyncResult result = wrappedAction.BeginInvoke(null, null);

        if (result.AsyncWaitHandle.WaitOne(timeoutMilliseconds))
        {
          wrappedAction.EndInvoke(result);
        }

      }
      catch (Exception)
      {

      }
    }

    private static void ConnectToProxyServers(Socket testSocket, IPEndPoint ipEndPoint)
    {
      try
      {
        if (testSocket == null || ipEndPoint == null)
          return;

        testSocket.Connect(ipEndPoint);

      }
      catch (Exception)
      {

      }
    }

    /// <summary>
    /// returns an the <see cref="T:TvControl.IController"/> interface to the tv server
    /// </summary>
    /// <value>The instance.</value>
    public static IController Instance
    {
      get
      {
        //Used for internal service operation as we have one server only. There is no need to go through the network.
        if (_tvControlLocal != null)
          return _tvControlLocal;

        lock (_Padlock)
        {
          try
          {
            if (_tvControl != null)
            {
              //only query state if the caller has subcribed to the disconnect/connect events
              if (OnRemotingDisconnected != null || OnRemotingConnected != null)
              {
                RefreshRemotingConnectionStatus();
                if (!_isRemotingConnected)
                  return null;
              }

              return _tvControl;
            }

            Log.Debug("RemoteControl: Instance: Creating TvControl to host: '{0}'", _hostName);

            _tvControl = CreateClient(_hostName);

            //only query state if the caller has subcribed to the disconnect/connect events
            if (OnRemotingDisconnected != null || OnRemotingConnected != null)
            {
              RefreshRemotingConnectionStatus();
              if (!_isRemotingConnected)
                return null;
            }

            Log.Debug("RemoteControl: Instance: Created.", _hostName);
            return _tvControl;
          }
          catch (Exception exg)
          {
            Log.Error("RemoteControl: Error getting server Instance - {0}", exg.Message);
            DisposeClient();
          }

          return null;
        }
      }
    }

    /// <summary>
    /// Set local service instance of IController. Used for internal service operation as we have one server only. There is no need to go through the network.
    /// </summary>
    public static IController InstanceLocal
    {
      set
      {
        _tvControlLocal = value;
      }
    }

    #endregion
  }
}
