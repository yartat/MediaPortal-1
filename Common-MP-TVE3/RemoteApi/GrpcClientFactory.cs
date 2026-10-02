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
using System.Threading.Tasks;
using Grpc.Core;

namespace MediaPortal.RemoteApi
{
  /// <summary>
  /// Creates authenticated TLS channels to the TV server's gRPC endpoints.
  /// </summary>
  public static class GrpcClientFactory
  {
    public const int MaxMessageSize = 16 * 1024 * 1024;

    /// <summary>
    /// Creates a channel to <paramref name="host"/>:<paramref name="port"/>.
    /// The connection key is <paramref name="connectionKey"/> or - for elevated tools on the TV server
    /// itself - the server's own key. The CA certificate is taken from the local key directory or fetched
    /// from the server and accepted only if it matches the pinned fingerprint.
    /// </summary>
    public static Channel CreateChannel(string host, int port, ConnectionKey connectionKey, int connectTimeoutMs)
    {
      ServerKeyMaterial local = GrpcSecurity.TryReadLocalMaterial();
      ConnectionKey key = connectionKey ?? (local != null ? local.ConnectionKey : null);
      if (key == null)
      {
        throw new InvalidOperationException(
          "No TV server connection key configured. Copy it from TV-Server Configuration (SetupTv) into the client configuration.");
      }

      string caPem = local != null && local.CaFingerprint == key.CaFingerprint
                       ? local.CaCertificatePem
                       : GrpcSecurity.FetchPinnedCaCertificatePem(host, port, key.CaFingerprint, connectTimeoutMs);

      string authorization = GrpcSecurity.BearerPrefix + key.ApiKey;
      CallCredentials apiKey = CallCredentials.FromInterceptor((context, metadata) =>
        {
          metadata.Add(GrpcSecurity.AuthorizationHeader, authorization);
          return Task.CompletedTask;
        });

      return new Channel(host, port, ChannelCredentials.Create(new SslCredentials(caPem), apiKey),
                         new[]
                           {
                             new ChannelOption(ChannelOptions.SslTargetNameOverride, GrpcSecurity.TargetNameOverride),
                             new ChannelOption(ChannelOptions.MaxReceiveMessageLength, MaxMessageSize),
                             new ChannelOption(ChannelOptions.MaxSendMessageLength, MaxMessageSize)
                           });
    }
  }
}
