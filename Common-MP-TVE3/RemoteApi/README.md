# TV server remote API (gRPC)

The TV server's remote interfaces use gRPC over TLS with an API key. They replace the
former .NET Remoting endpoints, which accepted unauthenticated `BinaryFormatter`/SOAP traffic.

| Endpoint | Port | Contract | Server | Clients |
|---|---|---|---|---|
| TvControl | 31456 | `TvEngine3/TVLibrary/TvControl/Grpc/tvcontrol.proto` | TvService | `RemoteControl.Instance` (TvPlugin, SetupTv, MP Configuration, tools) |
| PowerControl | 31457 | `Common-MP-TVE3/PowerScheduler.Interfaces/Grpc/powercontrol.proto` | PowerScheduler plugin in TvService | `RemotePowerControl` (PowerScheduler client plugin) |
| WatchDog | 9997 | `Common-MP-TVE3/WatchDogService.Interface/Grpc/watchdog.proto` | WatchDogService | MP WatchDog |

The client callback port 31458 that PowerScheduler used is gone. The client plugin now keeps a
`HandlerSession` stream open, and the server sends its standby and wakeup queries over that stream.

## Keys and certificates

On its first start the TV server (TvService or WatchDogService) creates the following files in
`%ProgramData%\Team MediaPortal\MediaPortal TV Server\grpc`. Only SYSTEM and Administrators can access them.

* `ca.crt`: a private CA (RSA 3072, valid for 20 years). Its private key is not stored.
* `server.crt` / `server.key`: the server certificate, issued by that CA for `mediaportal-tvserver`,
  plus the host names and IP addresses of the machine.
* `api.key`: a random 256-bit API key.

Every call must send `authorization: Bearer <api key>`. A call without it, or with a wrong key, is
rejected with `UNAUTHENTICATED` and logged.

## Connection key

Clients get one string, the **connection key**:

```
mptv1.<api key>.<SHA-256 fingerprint of ca.crt>
```

* TV-Server Configuration (SetupTv) shows it under *TV Servers → Client access*.
* MediaPortal Configuration stores it in `MediaPortal.xml` (`tvservice/connectionkey`). If the TV server
  runs on the same machine, the key is filled in automatically.
* Elevated tools that run on the TV server itself (SetupTv, MP Configuration, WatchDog) read the key
  directly when none is configured.

The client takes the CA from the local key directory, or fetches it during the TLS handshake and
accepts it only if its fingerprint matches the connection key (pinning). The TLS target name is
always `mediaportal-tvserver`, so clients can address the server by host name, FQDN or IP.

To revoke all clients, stop the TV service, delete the `grpc` directory and start the service again.
This creates a new connection key, which then has to be entered on every client.

## Building

Messages and stubs are generated from the `.proto` files at build time (`Grpc.targets`, using
`protoc` from the Grpc.Tools package). The server and clients run on .NET Framework 4.7.2, so
they use `Grpc.Core` 2.46.6. `Grpc.AspNetCore` and `Grpc.Net.Client` require .NET Core or newer.
Tests: `TvEngine3/TVLibrary/UnitTests/TvControlGrpcTests` (end-to-end over TLS):

```
Packages\NUnit.ConsoleRunner.3.22.0\tools\nunit3-console.exe TvEngine3\TVLibrary\UnitTests\TvControlGrpcTests\bin\Release\TvControlGrpcTests.dll --x86
```
