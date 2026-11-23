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

using System.IO;
using System.Threading.Tasks;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using WatchDogService.Interface.Proto;

namespace WatchDogService
{
  /// <summary>
  /// gRPC adapter for <see cref="WatchDogServer"/>.
  /// </summary>
  internal sealed class WatchDogGrpcService : WatchDog.WatchDogBase
  {
    private const int ChunkSize = 1024 * 1024;
    private static readonly Empty E = new Empty();
    private readonly WatchDogServer _server = new WatchDogServer();

    private static StringValue S(string value)
    {
      return new StringValue { Value = value ?? string.Empty };
    }

    public override Task<StringValue> StartTvService(Empty request, ServerCallContext context)
    {
      return Task.FromResult(S(_server.StartTVService()));
    }

    public override Task<StringValue> StopTvService(Empty request, ServerCallContext context)
    {
      return Task.FromResult(S(_server.StopTVService()));
    }

    public override async Task ReadLog(Empty request, IServerStreamWriter<LogChunk> responseStream, ServerCallContext context)
    {
      using (var zip = (MemoryStream)_server.ReadLog())
      {
        zip.Position = 0;
        var buffer = new byte[ChunkSize];
        int read;
        while ((read = zip.Read(buffer, 0, buffer.Length)) > 0)
          await responseStream.WriteAsync(new LogChunk { Data = ByteString.CopyFrom(buffer, 0, read) }).ConfigureAwait(false);
      }
    }

    public override Task<StringValue> ClearTvServerLogs(Empty request, ServerCallContext context)
    {
      return Task.FromResult(S(_server.ClearTVserverLogs()));
    }

    public override Task<StringValue> ClearWindowsEventLogs(Empty request, ServerCallContext context)
    {
      return Task.FromResult(S(_server.ClearWindowsEventLogs()));
    }

    public override Task<Empty> Reboot(Empty request, ServerCallContext context)
    {
      _server.Reboot();
      return Task.FromResult(E);
    }

    public override Task<Empty> Shutdown(Empty request, ServerCallContext context)
    {
      _server.Shutdown();
      return Task.FromResult(E);
    }

    public override Task<Empty> PowerOff(Empty request, ServerCallContext context)
    {
      _server.PowerOff();
      return Task.FromResult(E);
    }
  }
}
