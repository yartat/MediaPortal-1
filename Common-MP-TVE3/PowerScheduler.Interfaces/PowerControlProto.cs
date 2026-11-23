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

using P = TvEngine.PowerScheduler.Interfaces.Proto;

namespace TvEngine.PowerScheduler.Interfaces
{
  /// <summary>
  /// Converts power settings between the domain type and the gRPC message.
  /// </summary>
  public static class PowerControlProto
  {
    public static P.PowerSettings ToProto(IPowerSettings settings)
    {
      var p = new P.PowerSettings
                {
                  ShutdownEnabled = settings.ShutdownEnabled,
                  WakeupEnabled = settings.WakeupEnabled,
                  ForceShutdown = settings.ForceShutdown,
                  ExtensiveLogging = settings.ExtensiveLogging,
                  IdleTimeout = settings.IdleTimeout,
                  PreWakeupTime = settings.PreWakeupTime,
                  CheckInterval = settings.CheckInterval,
                  ShutdownMode = (int)settings.ShutdownMode
                };
      var full = settings as PowerSettings;
      if (full != null)
      {
        p.PreNoShutdownTime = full.PreNoShutdownTime;
        p.AllowedSleepStartTime = full.AllowedSleepStartTime;
        p.AllowedSleepStopTime = full.AllowedSleepStopTime;
        p.AllowedSleepStartTimeOnWeekend = full.AllowedSleepStartTimeOnWeekend;
        p.AllowedSleepStopTimeOnWeekend = full.AllowedSleepStopTimeOnWeekend;
        p.ReinitializeController = full.ReinitializeController;
      }
      return p;
    }

    public static PowerSettings FromProto(P.PowerSettings p)
    {
      var settings = new PowerSettings
                       {
                         ShutdownEnabled = p.ShutdownEnabled,
                         WakeupEnabled = p.WakeupEnabled,
                         ForceShutdown = p.ForceShutdown,
                         ExtensiveLogging = p.ExtensiveLogging,
                         IdleTimeout = p.IdleTimeout,
                         PreWakeupTime = p.PreWakeupTime,
                         PreNoShutdownTime = p.PreNoShutdownTime,
                         ShutdownMode = (ShutdownMode)p.ShutdownMode,
                         AllowedSleepStartTime = p.AllowedSleepStartTime,
                         AllowedSleepStopTime = p.AllowedSleepStopTime,
                         AllowedSleepStartTimeOnWeekend = p.AllowedSleepStartTimeOnWeekend,
                         AllowedSleepStopTimeOnWeekend = p.AllowedSleepStopTimeOnWeekend,
                         ReinitializeController = p.ReinitializeController
                       };
      // 0 = never set on the server; the setter only accepts values >= 1
      if (p.CheckInterval > 0)
        settings.CheckInterval = p.CheckInterval;
      return settings;
    }
  }
}
