#region Copyright (C) 2005-2011 Team MediaPortal

// Copyright (C) 2005-2011 Team MediaPortal
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

#region Usings

using System;
using MediaPortal.RemoteApi;

#endregion

namespace TvEngine.PowerScheduler.Interfaces
{
  /// <summary>
  /// Class which holds the connection with powerscheduler in the tvengine (gRPC over TLS, authenticated with
  /// the TV server connection key)
  /// </summary>
  public class RemotePowerControl
  {
    /// <summary>TCP port of the PowerControl gRPC endpoint.</summary>
    public const int Port = 31457;

    private const int ConnectTimeoutMs = 3000;

    #region Variables

    /// <summary>
    /// IPowerController singleton
    /// </summary>
    private static GrpcPowerController _powerController;

    private static string _hostName = "localhost";
    private static MediaPortal.RemoteApi.ConnectionKey _connectionKey;
    private static readonly object _lock = new object();

    #endregion

    #region Public Properties

    /// <summary>
    /// Hostname of the tvserver
    /// </summary>
    public static string HostName
    {
      get { return _hostName; }
      set
      {
        lock (_lock)
        {
          if (_hostName != value)
          {
            Clear();
            _hostName = value;
          }
        }
      }
    }

    /// <summary>
    /// Connection key of the tvserver (see MediaPortal.RemoteApi.ConnectionKey). When not set, the key of a
    /// tvserver on this machine is used if the process may read it.
    /// </summary>
    public static string ConnectionKey
    {
      get { return _connectionKey == null ? null : _connectionKey.ToString(); }
      set
      {
        lock (_lock)
        {
          MediaPortal.RemoteApi.ConnectionKey key;
          MediaPortal.RemoteApi.ConnectionKey.TryParse(value, out key);
          if ((key == null ? null : key.ToString()) == ConnectionKey)
            return;
          Clear();
          _connectionKey = key;
        }
      }
    }

    /// <summary>
    /// Optional sink for diagnostic messages of the handler session.
    /// </summary>
    public static Action<string> Log { get; set; }

    /// <summary>
    /// Returns the one and only instance of the IPowerController (PowerScheduler)
    /// </summary>
    public static IPowerController Instance
    {
      get
      {
        lock (_lock)
        {
          try
          {
            if (_powerController == null)
            {
              _powerController =
                new GrpcPowerController(GrpcClientFactory.CreateChannel(_hostName, Port, _connectionKey, ConnectTimeoutMs));
            }
            return _powerController;
          }
          catch (Exception ex)
          {
            if (Log != null)
              Log("PS: cannot connect to the server's PowerScheduler - " + ex.Message);
            return _powerController;
          }
        }
      }
    }

    /// <summary>
    /// Is the RemotePowerControl client connected to the server?
    /// </summary>
    public static bool Isconnected
    {
      get
      {
        try
        {
          GrpcPowerController controller = _powerController;
          if (controller == null)
          {
            return false;
          }
          return controller.IsConnected;
        }
        catch (Exception)
        {
          return false;
        }
      }
    }

    #endregion

    #region Public methods

    /// <summary>
    /// Lets the server's PowerScheduler query the given standby/wakeup handlers (single-seat client plugin).
    /// Replaces the former remoting callback registration (RegisterRemote).
    /// </summary>
    public static void StartHandlerSession(IStandbyHandler standbyHandler, IWakeupHandler wakeupHandler)
    {
      var controller = Instance as GrpcPowerController;
      if (controller != null)
        controller.StartHandlerSession(standbyHandler, wakeupHandler, Log);
    }

    /// <summary>
    /// Ends the handler session started with <see cref="StartHandlerSession"/>.
    /// </summary>
    public static void StopHandlerSession()
    {
      GrpcPowerController controller = _powerController;
      if (controller != null)
        controller.StopHandlerSession();
    }

    public static void Clear()
    {
      lock (_lock)
      {
        GrpcPowerController controller = _powerController;
        _powerController = null;
        if (controller != null)
        {
          try
          {
            controller.Dispose();
          }
          catch (Exception) {}
        }
      }
    }

    #endregion
  }
}
