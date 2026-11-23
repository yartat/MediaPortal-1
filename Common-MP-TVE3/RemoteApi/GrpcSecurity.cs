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
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.AccessControl;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Security.Principal;
using System.Text;
using System.Threading;

namespace MediaPortal.RemoteApi
{
  /// <summary>
  /// TLS material and API key of a TV server.
  /// </summary>
  public sealed class ServerKeyMaterial
  {
    public string CaCertificatePem { get; internal set; }
    /// <summary>Server certificate followed by the CA certificate (PEM chain).</summary>
    public string ServerCertificateChainPem { get; internal set; }
    public string ServerPrivateKeyPem { get; internal set; }
    public string ApiKey { get; internal set; }
    public string CaFingerprint { get; internal set; }

    public ConnectionKey ConnectionKey
    {
      get { return new ConnectionKey(ApiKey, CaFingerprint); }
    }
  }

  /// <summary>
  /// Key/certificate handling for the gRPC endpoints of the TV server.
  ///
  /// The server creates (once) a private CA, a server certificate issued by it and a random API key.
  /// They live in %ProgramData%\Team MediaPortal\MediaPortal TV Server\grpc, readable only by SYSTEM and
  /// Administrators. Clients authenticate with the API key ("authorization: Bearer ...") and pin the CA
  /// by its SHA-256 fingerprint; both are part of the <see cref="ConnectionKey"/>.
  /// </summary>
  public static class GrpcSecurity
  {
    public const string AuthorizationHeader = "authorization";
    public const string BearerPrefix = "Bearer ";

    /// <summary>
    /// Name the server certificate is always valid for. Clients use it as TLS target name, so it does
    /// not matter whether they address the server by host name, FQDN or IP. Safe because the CA is pinned.
    /// </summary>
    public const string TargetNameOverride = "mediaportal-tvserver";

    private const string CaCertificateFileName = "ca.crt";
    private const string ServerCertificateFileName = "server.crt";
    private const string ServerKeyFileName = "server.key";
    private const string ApiKeyFileName = "api.key";
    private const int ValidityYears = 20;

    private static readonly object FileLock = new object();

    public static string KeyDirectory
    {
      get
      {
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                            @"Team MediaPortal\MediaPortal TV Server\grpc");
      }
    }

    #region server side

    /// <summary>
    /// Loads the server key material, creating it on first use (or when the certificate expired).
    /// TvService and WatchDogService share the material; a machine wide mutex keeps them from both creating it.
    /// </summary>
    public static ServerKeyMaterial LoadOrCreateServerMaterial()
    {
      using (var mutex = new Mutex(false, @"Global\MediaPortalRemoteApiKeyMaterial"))
      {
        bool owned = false;
        try
        {
          try
          {
            owned = mutex.WaitOne(TimeSpan.FromSeconds(30));
          }
          catch (AbandonedMutexException)
          {
            owned = true;
          }
          lock (FileLock)
          {
            ServerKeyMaterial material = TryLoadMaterial(true);
            if (material != null)
              return material;

            material = CreateServerMaterial();
            Directory.CreateDirectory(KeyDirectory);
            RestrictDirectoryToAdministrators(KeyDirectory);
            File.WriteAllText(Path.Combine(KeyDirectory, CaCertificateFileName), material.CaCertificatePem, Encoding.ASCII);
            File.WriteAllText(Path.Combine(KeyDirectory, ServerCertificateFileName), material.ServerCertificateChainPem,
                              Encoding.ASCII);
            File.WriteAllText(Path.Combine(KeyDirectory, ServerKeyFileName), material.ServerPrivateKeyPem, Encoding.ASCII);
            File.WriteAllText(Path.Combine(KeyDirectory, ApiKeyFileName), material.ApiKey, Encoding.ASCII);
            return material;
          }
        }
        finally
        {
          if (owned)
            mutex.ReleaseMutex();
        }
      }
    }

    /// <summary>
    /// Creates a new CA, server certificate and API key in memory (nothing is written to disk).
    /// </summary>
    public static ServerKeyMaterial CreateServerMaterial()
    {
      DateTimeOffset notBefore = DateTimeOffset.UtcNow.AddDays(-1);
      DateTimeOffset notAfter = notBefore.AddYears(ValidityYears);

      using (RSA caKey = new RSACryptoServiceProvider(3072))
      using (RSA serverKey = new RSACryptoServiceProvider(2048))
      {
        var caRequest = new CertificateRequest("CN=MediaPortal TV Server CA (" + Dns.GetHostName() + ")", caKey,
                                               HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        caRequest.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, true, 0, true));
        caRequest.CertificateExtensions.Add(
          new X509KeyUsageExtension(X509KeyUsageFlags.KeyCertSign | X509KeyUsageFlags.CrlSign, true));
        caRequest.CertificateExtensions.Add(new X509SubjectKeyIdentifierExtension(caRequest.PublicKey, false));

        using (X509Certificate2 ca = caRequest.CreateSelfSigned(notBefore, notAfter))
        {
          var serverRequest = new CertificateRequest("CN=" + TargetNameOverride, serverKey, HashAlgorithmName.SHA256,
                                                     RSASignaturePadding.Pkcs1);
          serverRequest.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
          serverRequest.CertificateExtensions.Add(
            new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, true));
          serverRequest.CertificateExtensions.Add(
            new X509EnhancedKeyUsageExtension(new OidCollection { new Oid("1.3.6.1.5.5.7.3.1") }, false));
          serverRequest.CertificateExtensions.Add(BuildSubjectAlternativeNames());

          byte[] serial = new byte[16];
          using (var rng = RandomNumberGenerator.Create())
            rng.GetBytes(serial);
          serial[0] &= 0x7F; // positive

          using (X509Certificate2 server = serverRequest.Create(ca, notBefore, notAfter.AddDays(-1), serial))
          {
            string caPem = ToPem("CERTIFICATE", ca.RawData);
            return new ServerKeyMaterial
                     {
                       CaCertificatePem = caPem,
                       ServerCertificateChainPem = ToPem("CERTIFICATE", server.RawData) + caPem,
                       ServerPrivateKeyPem = ToPem("RSA PRIVATE KEY", EncodeRsaPrivateKey(serverKey.ExportParameters(true))),
                       ApiKey = CreateApiKey(),
                       CaFingerprint = Fingerprint(ca)
                     };
          }
        }
      }
    }

    private static X509Extension BuildSubjectAlternativeNames()
    {
      var san = new SubjectAlternativeNameBuilder();
      var dnsNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { TargetNameOverride, "localhost" };
      try
      {
        dnsNames.Add(Dns.GetHostName());
        dnsNames.Add(Dns.GetHostEntry(Dns.GetHostName()).HostName);
      }
      catch (SocketException) {}
      foreach (string name in dnsNames)
        san.AddDnsName(name);

      san.AddIpAddress(IPAddress.Loopback);
      san.AddIpAddress(IPAddress.IPv6Loopback);
      foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
      {
        foreach (UnicastIPAddressInformation address in nic.GetIPProperties().UnicastAddresses)
        {
          if (!IPAddress.IsLoopback(address.Address))
            san.AddIpAddress(address.Address);
        }
      }
      return san.Build();
    }

    private static string CreateApiKey()
    {
      byte[] bytes = new byte[32];
      using (var rng = RandomNumberGenerator.Create())
        rng.GetBytes(bytes);
      return Convert.ToBase64String(bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_');
    }

    private static void RestrictDirectoryToAdministrators(string directory)
    {
      var security = new DirectorySecurity();
      security.SetAccessRuleProtection(true, false);
      foreach (WellKnownSidType sid in new[] { WellKnownSidType.LocalSystemSid, WellKnownSidType.BuiltinAdministratorsSid })
      {
        security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(sid, null), FileSystemRights.FullControl,
                                                        InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                                                        PropagationFlags.None, AccessControlType.Allow));
      }
      Directory.SetAccessControl(directory, security);
    }

    #endregion

    #region client side

    /// <summary>
    /// Reads the connection key and CA certificate of a TV server on this machine. Works for elevated
    /// processes only (SetupTv, MediaPortal Configuration); returns null otherwise.
    /// </summary>
    public static ServerKeyMaterial TryReadLocalMaterial()
    {
      lock (FileLock)
      {
        try
        {
          return TryLoadMaterial(false);
        }
        catch (UnauthorizedAccessException)
        {
          return null;
        }
        catch (IOException)
        {
          return null;
        }
      }
    }

    /// <summary>
    /// Fetches the CA certificate presented by the server and accepts it only if its fingerprint matches.
    /// </summary>
    public static string FetchPinnedCaCertificatePem(string host, int port, string caFingerprint, int timeoutMs)
    {
      X509Certificate2 pinned = null;
      using (var tcp = new TcpClient())
      {
        IAsyncResult connect = tcp.BeginConnect(host, port, null, null);
        if (!connect.AsyncWaitHandle.WaitOne(timeoutMs))
          throw new TimeoutException(string.Format("Timeout connecting to {0}:{1}", host, port));
        tcp.EndConnect(connect);
        tcp.ReceiveTimeout = timeoutMs;
        tcp.SendTimeout = timeoutMs;

        RemoteCertificateValidationCallback validate = (sender, certificate, chain, errors) =>
          {
            if (chain == null)
              return false;
            foreach (X509ChainElement element in chain.ChainElements)
            {
              if (Fingerprint(element.Certificate) == caFingerprint)
              {
                pinned = new X509Certificate2(element.Certificate.RawData);
                return true;
              }
            }
            return false;
          };

        using (var ssl = new SslStream(tcp.GetStream(), false, validate))
        {
          try
          {
            ssl.AuthenticateAsClient(TargetNameOverride, null, SslProtocols.Tls12, false);
          }
          catch (IOException)
          {
            // The gRPC server may drop a plain TLS client right after the handshake - the certificate was seen already.
            if (pinned == null)
              throw;
          }
        }
      }
      if (pinned == null)
        throw new AuthenticationException("TV server certificate does not match the pinned CA fingerprint");
      using (pinned)
        return ToPem("CERTIFICATE", pinned.RawData);
    }

    #endregion

    #region helpers

    public static string Fingerprint(X509Certificate certificate)
    {
      using (SHA256 sha = SHA256.Create())
        return ToHex(sha.ComputeHash(certificate.GetRawCertData()));
    }

    /// <summary>Constant-time comparison of two secrets.</summary>
    public static bool FixedTimeEquals(string a, string b)
    {
      if (a == null || b == null)
        return false;
      byte[] x = Encoding.UTF8.GetBytes(a);
      byte[] y = Encoding.UTF8.GetBytes(b);
      int diff = x.Length ^ y.Length;
      for (int i = 0; i < x.Length && i < y.Length; i++)
        diff |= x[i] ^ y[i];
      return diff == 0;
    }

    private static ServerKeyMaterial TryLoadMaterial(bool requireValidCertificate)
    {
      string caFile = Path.Combine(KeyDirectory, CaCertificateFileName);
      string serverFile = Path.Combine(KeyDirectory, ServerCertificateFileName);
      string keyFile = Path.Combine(KeyDirectory, ServerKeyFileName);
      string apiKeyFile = Path.Combine(KeyDirectory, ApiKeyFileName);
      if (!File.Exists(caFile) || !File.Exists(apiKeyFile))
        return null;
      if (requireValidCertificate && (!File.Exists(serverFile) || !File.Exists(keyFile)))
        return null;

      string caPem = File.ReadAllText(caFile, Encoding.ASCII);
      var material = new ServerKeyMaterial
                       {
                         CaCertificatePem = caPem,
                         ApiKey = File.ReadAllText(apiKeyFile, Encoding.ASCII).Trim()
                       };
      using (var ca = new X509Certificate2(FromPem(caPem)))
        material.CaFingerprint = Fingerprint(ca);

      if (requireValidCertificate)
      {
        material.ServerCertificateChainPem = File.ReadAllText(serverFile, Encoding.ASCII);
        material.ServerPrivateKeyPem = File.ReadAllText(keyFile, Encoding.ASCII);
        using (var server = new X509Certificate2(FromPem(material.ServerCertificateChainPem)))
        {
          if (server.NotAfter.ToUniversalTime() < DateTime.UtcNow.AddDays(1))
            return null;
        }
      }
      return material;
    }

    private static string ToHex(byte[] bytes)
    {
      var sb = new StringBuilder(bytes.Length * 2);
      foreach (byte b in bytes)
        sb.Append(b.ToString("X2"));
      return sb.ToString();
    }

    private static string ToPem(string label, byte[] der)
    {
      string base64 = Convert.ToBase64String(der);
      var sb = new StringBuilder();
      sb.Append("-----BEGIN ").Append(label).Append("-----\n");
      for (int i = 0; i < base64.Length; i += 64)
        sb.Append(base64, i, Math.Min(64, base64.Length - i)).Append('\n');
      sb.Append("-----END ").Append(label).Append("-----\n");
      return sb.ToString();
    }

    /// <summary>DER bytes of the first PEM block.</summary>
    private static byte[] FromPem(string pem)
    {
      int start = pem.IndexOf("-----", StringComparison.Ordinal);
      start = pem.IndexOf('\n', start) + 1;
      int end = pem.IndexOf("-----END", start, StringComparison.Ordinal);
      return Convert.FromBase64String(pem.Substring(start, end - start).Replace("\n", "").Replace("\r", ""));
    }

    /// <summary>PKCS#1 RSAPrivateKey (what gRPC/BoringSSL accepts as "RSA PRIVATE KEY").</summary>
    private static byte[] EncodeRsaPrivateKey(RSAParameters p)
    {
      var body = new MemoryStream();
      WriteDerInteger(body, new byte[] { 0 });
      foreach (byte[] value in new[] { p.Modulus, p.Exponent, p.D, p.P, p.Q, p.DP, p.DQ, p.InverseQ })
        WriteDerInteger(body, value);
      var sequence = new MemoryStream();
      sequence.WriteByte(0x30);
      WriteDerLength(sequence, (int)body.Length);
      body.WriteTo(sequence);
      return sequence.ToArray();
    }

    private static void WriteDerInteger(Stream s, byte[] bigEndianUnsigned)
    {
      int skip = 0;
      while (skip < bigEndianUnsigned.Length - 1 && bigEndianUnsigned[skip] == 0)
        skip++;
      bool pad = (bigEndianUnsigned[skip] & 0x80) != 0;
      int length = bigEndianUnsigned.Length - skip + (pad ? 1 : 0);
      s.WriteByte(0x02);
      WriteDerLength(s, length);
      if (pad)
        s.WriteByte(0);
      s.Write(bigEndianUnsigned, skip, bigEndianUnsigned.Length - skip);
    }

    private static void WriteDerLength(Stream s, int length)
    {
      if (length < 0x80)
      {
        s.WriteByte((byte)length);
        return;
      }
      var bytes = new List<byte>();
      for (int l = length; l > 0; l >>= 8)
        bytes.Insert(0, (byte)(l & 0xFF));
      s.WriteByte((byte)(0x80 | bytes.Count));
      foreach (byte b in bytes)
        s.WriteByte(b);
    }

    #endregion
  }
}
