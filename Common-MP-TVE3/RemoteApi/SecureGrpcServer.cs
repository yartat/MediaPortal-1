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
using System.Linq;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Interceptors;

namespace MediaPortal.RemoteApi
{
  /// <summary>
  /// Rejects every call that does not carry the server's API key ("authorization: Bearer &lt;key&gt;").
  /// </summary>
  public sealed class ApiKeyInterceptor : Interceptor
  {
    private readonly string _expected;
    private readonly Action<string> _log;

    public ApiKeyInterceptor(string apiKey, Action<string> log)
    {
      _expected = GrpcSecurity.BearerPrefix + apiKey;
      _log = log;
    }

    private void Authorize(ServerCallContext context)
    {
      Metadata.Entry entry = context.RequestHeaders.FirstOrDefault(h => h.Key == GrpcSecurity.AuthorizationHeader);
      if (entry == null || !GrpcSecurity.FixedTimeEquals(entry.Value, _expected))
      {
        if (_log != null)
          _log(string.Format("gRPC: rejected unauthenticated call {0} from {1}", context.Method, context.Peer));
        throw new RpcException(new Status(StatusCode.Unauthenticated, "Invalid or missing API key"));
      }
    }

    public override Task<TResponse> UnaryServerHandler<TRequest, TResponse>(TRequest request, ServerCallContext context,
                                                                            UnaryServerMethod<TRequest, TResponse> continuation)
    {
      Authorize(context);
      return continuation(request, context);
    }

    public override Task ServerStreamingServerHandler<TRequest, TResponse>(TRequest request,
                                                                           IServerStreamWriter<TResponse> responseStream,
                                                                           ServerCallContext context,
                                                                           ServerStreamingServerMethod<TRequest, TResponse> continuation)
    {
      Authorize(context);
      return continuation(request, responseStream, context);
    }

    public override Task<TResponse> ClientStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream,
                                                                                      ServerCallContext context,
                                                                                      ClientStreamingServerMethod<TRequest, TResponse> continuation)
    {
      Authorize(context);
      return continuation(requestStream, context);
    }

    public override Task DuplexStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream,
                                                                           IServerStreamWriter<TResponse> responseStream,
                                                                           ServerCallContext context,
                                                                           DuplexStreamingServerMethod<TRequest, TResponse> continuation)
    {
      Authorize(context);
      return continuation(requestStream, responseStream, context);
    }
  }

  /// <summary>
  /// Builds gRPC servers that only accept TLS connections and calls authenticated with the API key.
  /// </summary>
  public static class SecureGrpcServer
  {
    /// <param name="log">Receives a line for every rejected call (may be null).</param>
    public static Server Create(int port, ServerKeyMaterial material, Action<string> log,
                                params ServerServiceDefinition[] services)
    {
      var credentials = new SslServerCredentials(new[]
                                                   {
                                                     new KeyCertificatePair(material.ServerCertificateChainPem,
                                                                            material.ServerPrivateKeyPem)
                                                   });
      var server = new Server(new[]
                                {
                                  new ChannelOption(ChannelOptions.MaxReceiveMessageLength, GrpcClientFactory.MaxMessageSize),
                                  new ChannelOption(ChannelOptions.MaxSendMessageLength, GrpcClientFactory.MaxMessageSize)
                                });
      server.Ports.Add(new ServerPort("0.0.0.0", port, credentials));
      var interceptor = new ApiKeyInterceptor(material.ApiKey, log);
      foreach (ServerServiceDefinition service in services)
        server.Services.Add(service.Intercept(interceptor));
      return server;
    }
  }
}
