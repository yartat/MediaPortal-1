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
using System.Text.RegularExpressions;

namespace MediaPortal.RemoteApi
{
  /// <summary>
  /// Everything a client needs to talk to a TV server: the API key that authenticates it and the
  /// SHA-256 fingerprint of the server's CA certificate that the client pins.
  /// Text form (what users copy from SetupTv): <c>mptv1.&lt;api key&gt;.&lt;fingerprint hex&gt;</c>
  /// </summary>
  public sealed class ConnectionKey
  {
    private const string Prefix = "mptv1";
    private static readonly Regex Format =
      new Regex(@"^mptv1\.(?<key>[A-Za-z0-9_-]{32,128})\.(?<fp>[0-9a-fA-F]{64})$", RegexOptions.CultureInvariant);

    public ConnectionKey(string apiKey, string caFingerprint)
    {
      if (string.IsNullOrEmpty(apiKey))
        throw new ArgumentException("API key is empty", "apiKey");
      if (string.IsNullOrEmpty(caFingerprint) || caFingerprint.Length != 64)
        throw new ArgumentException("CA fingerprint must be a SHA-256 hex string", "caFingerprint");
      ApiKey = apiKey;
      CaFingerprint = caFingerprint.ToUpperInvariant();
    }

    public string ApiKey { get; private set; }

    /// <summary>Upper-case hex SHA-256 of the CA certificate (DER).</summary>
    public string CaFingerprint { get; private set; }

    public static bool TryParse(string text, out ConnectionKey key)
    {
      key = null;
      if (string.IsNullOrWhiteSpace(text))
        return false;
      Match m = Format.Match(text.Trim());
      if (!m.Success)
        return false;
      key = new ConnectionKey(m.Groups["key"].Value, m.Groups["fp"].Value);
      return true;
    }

    public override string ToString()
    {
      return Prefix + "." + ApiKey + "." + CaFingerprint;
    }
  }
}
