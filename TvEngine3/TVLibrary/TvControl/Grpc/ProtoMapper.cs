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
using DirectShowLib;
using DirectShowLib.BDA;
using TvLibrary;
using TvLibrary.Channels;
using TvLibrary.Epg;
using TvLibrary.Implementations;
using TvLibrary.Implementations.DVB;
using TvLibrary.Interfaces;
using TvLibrary.Streaming;
using P = TvControl.Grpc.Proto;

namespace TvControl.Grpc
{
  /// <summary>
  /// Converts between the TvControl/TvLibrary domain types and the generated protobuf messages.
  /// Used by both the client proxy and the server implementation.
  /// </summary>
  public static class ProtoMapper
  {
    #region strings / primitives

    /// <summary>proto3 strings cannot be null.</summary>
    public static string S(string value)
    {
      return value ?? string.Empty;
    }

    public static P.NullableStringReply ToNullableString(string value)
    {
      return new P.NullableStringReply { Value = S(value), IsNull = value == null };
    }

    public static string FromNullableString(P.NullableStringReply reply)
    {
      return reply.IsNull ? null : reply.Value;
    }

    public static P.NullableBytesReply ToNullableBytes(byte[] value)
    {
      return new P.NullableBytesReply
               {
                 Value = value == null ? Google.Protobuf.ByteString.Empty : Google.Protobuf.ByteString.CopyFrom(value),
                 IsNull = value == null
               };
    }

    public static byte[] FromNullableBytes(P.NullableBytesReply reply)
    {
      return reply.IsNull ? null : reply.Value.ToByteArray();
    }

    #endregion

    #region User

    public static P.User ToProto(IUser user)
    {
      if (user == null)
        return null;
      var p = new P.User
                {
                  Name = S(user.Name),
                  IsAdmin = user.IsAdmin,
                  CardId = user.CardId,
                  FailedCardId = user.FailedCardId,
                  SubChannel = user.SubChannel,
                  IsFreeToAir = user.IsFreeToAir,
                  IdChannel = user.IdChannel,
                  TvStoppedReason = (P.TvStoppedReason)user.TvStoppedReason,
                  HeartBeatTicks = user.HeartBeat.Ticks,
                  HasChannelStates = user.ChannelStates != null
                };
      if (user.Priority.HasValue)
        p.Priority = user.Priority.Value;
      if (user.ChannelStates != null)
      {
        foreach (KeyValuePair<int, ChannelState> state in user.ChannelStates)
          p.ChannelStates.Add(state.Key, (P.ChannelState)state.Value);
      }
      return p;
    }

    /// <summary>
    /// Note: IUser.History is server-side state and is not transported.
    /// </summary>
    public static User FromProto(P.User p)
    {
      if (p == null)
        return null;
      var user = new User
                   {
                     Name = p.Name,
                     IsAdmin = p.IsAdmin,
                     CardId = p.CardId,
                     FailedCardId = p.FailedCardId,
                     SubChannel = p.SubChannel,
                     IsFreeToAir = p.IsFreeToAir,
                     IdChannel = p.IdChannel,
                     TvStoppedReason = (TvStoppedReason)p.TvStoppedReason,
                     HeartBeat = new DateTime(p.HeartBeatTicks),
                     Priority = p.HasPriority ? p.Priority : (int?)null,
                     ChannelStates = p.HasChannelStates ? FromProto(p.ChannelStates) : null
                   };
      return user;
    }

    public static Dictionary<int, ChannelState> FromProto(IDictionary<int, P.ChannelState> states)
    {
      var result = new Dictionary<int, ChannelState>(states.Count);
      foreach (KeyValuePair<int, P.ChannelState> state in states)
        result[state.Key] = (ChannelState)state.Value;
      return result;
    }

    public static P.ChannelStateMap ToProto(Dictionary<int, ChannelState> states)
    {
      var map = new P.ChannelStateMap { IsNull = states == null };
      if (states != null)
      {
        foreach (KeyValuePair<int, ChannelState> state in states)
          map.States.Add(state.Key, (P.ChannelState)state.Value);
      }
      return map;
    }

    public static Dictionary<int, ChannelState> FromProto(P.ChannelStateMap map)
    {
      return map.IsNull ? null : FromProto(map.States);
    }

    #endregion

    #region VirtualCard

    public static P.VirtualCard ToProto(VirtualCard card)
    {
      if (card == null)
        return null;
      var p = new P.VirtualCard
                {
                  User = ToProto(card.User),
                  RemoteServer = S(card.RemoteServer),
                  RecordingFormat = card.RecordingFormat,
                  NrOfOtherUsersTimeshiftingOnCard = card.NrOfOtherUsersTimeshiftingOnCard
                };
      if (card.RecordingFolder != null)
        p.RecordingFolder = card.RecordingFolder;
      if (card.TimeshiftFolder != null)
        p.TimeshiftFolder = card.TimeshiftFolder;
      return p;
    }

    public static VirtualCard FromProto(P.VirtualCard p)
    {
      if (p == null)
        return null;
      var card = new VirtualCard(FromProto(p.User), p.RemoteServer)
                   {
                     RecordingFormat = p.RecordingFormat,
                     NrOfOtherUsersTimeshiftingOnCard = p.NrOfOtherUsersTimeshiftingOnCard
                   };
      if (p.HasRecordingFolder)
        card.RecordingFolder = p.RecordingFolder;
      if (p.HasTimeshiftFolder)
        card.TimeshiftFolder = p.TimeshiftFolder;
      return card;
    }

    #endregion

    #region Channels

    public static P.Channel ToProto(IChannel channel)
    {
      if (channel == null)
        return null;

      // Most derived types first.
      var dvbs = channel as DVBSChannel;
      if (dvbs != null)
        return new P.Channel
                 {
                   DvbS = new P.DvbSChannel
                            {
                              Base = ToProtoBase(dvbs),
                              Polarisation = (int)dvbs.Polarisation,
                              SymbolRate = dvbs.SymbolRate,
                              SwitchingFrequency = dvbs.SwitchingFrequency,
                              Diseqc = (int)dvbs.DisEqc,
                              BandType = (int)dvbs.BandType,
                              Modulation = (int)dvbs.ModulationType,
                              InnerFecRate = (int)dvbs.InnerFecRate,
                              Pilot = (int)dvbs.Pilot,
                              RollOff = (int)dvbs.Rolloff,
                              SatelliteIndex = dvbs.SatelliteIndex
                            }
                 };

      var dvbc = channel as DVBCChannel;
      if (dvbc != null)
        return new P.Channel
                 {
                   DvbC = new P.DvbCChannel
                            {
                              Base = ToProtoBase(dvbc),
                              Modulation = (int)dvbc.ModulationType,
                              SymbolRate = dvbc.SymbolRate
                            }
                 };

      var dvbt = channel as DVBTChannel;
      if (dvbt != null)
        return new P.Channel
                 {
                   DvbT = new P.DvbTChannel { Base = ToProtoBase(dvbt), BandWidth = dvbt.BandWidth, Offset = dvbt.Offset }
                 };

      var atsc = channel as ATSCChannel;
      if (atsc != null)
        return new P.Channel
                 {
                   Atsc = new P.AtscChannel
                            {
                              Base = ToProtoBase(atsc),
                              PhysicalChannel = atsc.PhysicalChannel,
                              MajorChannel = atsc.MajorChannel,
                              MinorChannel = atsc.MinorChannel,
                              Modulation = (int)atsc.ModulationType
                            }
                 };

      var dvbip = channel as DVBIPChannel;
      if (dvbip != null)
        return new P.Channel { DvbIp = new P.DvbIpChannel { Base = ToProtoBase(dvbip), Url = S(dvbip.Url) } };

      var analog = channel as AnalogChannel;
      if (analog != null)
        return new P.Channel
                 {
                   Analog = new P.AnalogChannel
                              {
                                Name = S(analog.Name),
                                Frequency = analog.Frequency,
                                ChannelNumber = analog.ChannelNumber,
                                Country = ToProto(analog.Country),
                                IsRadio = analog.IsRadio,
                                TunerSource = (int)analog.TunerSource,
                                VideoSource = (int)analog.VideoSource,
                                AudioSource = (int)analog.AudioSource,
                                IsVcrSignal = analog.IsVCRSignal
                              }
                 };

      var web = channel as RadioWebStreamChannel;
      if (web != null)
        return new P.Channel
                 {
                   RadioWebStream = new P.RadioWebStreamChannel
                                      {
                                        Name = S(web.Name),
                                        Url = S(web.Url),
                                        Country = ToProto(web.Country)
                                      }
                 };

      throw new NotSupportedException("Channel type cannot be transported: " + channel.GetType().FullName);
    }

    public static IChannel FromProto(P.Channel p)
    {
      if (p == null)
        return null;
      switch (p.KindCase)
      {
        case P.Channel.KindOneofCase.DvbS:
          {
            var c = new DVBSChannel();
            FromProtoBase(p.DvbS.Base, c);
            c.Polarisation = (Polarisation)p.DvbS.Polarisation;
            c.SymbolRate = p.DvbS.SymbolRate;
            c.SwitchingFrequency = p.DvbS.SwitchingFrequency;
            c.DisEqc = (DisEqcType)p.DvbS.Diseqc;
            c.BandType = (BandType)p.DvbS.BandType;
            c.ModulationType = (ModulationType)p.DvbS.Modulation;
            c.InnerFecRate = (BinaryConvolutionCodeRate)p.DvbS.InnerFecRate;
            c.Pilot = (Pilot)p.DvbS.Pilot;
            c.Rolloff = (RollOff)p.DvbS.RollOff;
            c.SatelliteIndex = p.DvbS.SatelliteIndex;
            return c;
          }
        case P.Channel.KindOneofCase.DvbC:
          {
            var c = new DVBCChannel();
            FromProtoBase(p.DvbC.Base, c);
            c.ModulationType = (ModulationType)p.DvbC.Modulation;
            c.SymbolRate = p.DvbC.SymbolRate;
            return c;
          }
        case P.Channel.KindOneofCase.DvbT:
          {
            var c = new DVBTChannel();
            FromProtoBase(p.DvbT.Base, c);
            c.BandWidth = p.DvbT.BandWidth;
            c.Offset = p.DvbT.Offset;
            return c;
          }
        case P.Channel.KindOneofCase.Atsc:
          {
            var c = new ATSCChannel();
            FromProtoBase(p.Atsc.Base, c);
            c.PhysicalChannel = p.Atsc.PhysicalChannel;
            c.MajorChannel = p.Atsc.MajorChannel;
            c.MinorChannel = p.Atsc.MinorChannel;
            c.ModulationType = (ModulationType)p.Atsc.Modulation;
            return c;
          }
        case P.Channel.KindOneofCase.DvbIp:
          {
            var c = new DVBIPChannel();
            FromProtoBase(p.DvbIp.Base, c);
            c.Url = p.DvbIp.Url;
            return c;
          }
        case P.Channel.KindOneofCase.Analog:
          return new AnalogChannel
                   {
                     Name = p.Analog.Name,
                     Frequency = p.Analog.Frequency,
                     ChannelNumber = p.Analog.ChannelNumber,
                     Country = FromProto(p.Analog.Country),
                     IsRadio = p.Analog.IsRadio,
                     TunerSource = (TunerInputType)p.Analog.TunerSource,
                     VideoSource = (AnalogChannel.VideoInputType)p.Analog.VideoSource,
                     AudioSource = (AnalogChannel.AudioInputType)p.Analog.AudioSource,
                     IsVCRSignal = p.Analog.IsVcrSignal
                   };
        case P.Channel.KindOneofCase.RadioWebStream:
          return new RadioWebStreamChannel
                   {
                     Name = p.RadioWebStream.Name,
                     Url = p.RadioWebStream.Url,
                     Country = FromProto(p.RadioWebStream.Country)
                   };
        default:
          return null;
      }
    }

    public static IChannel[] FromProto(P.ChannelList list)
    {
      var channels = new IChannel[list.Channels.Count];
      for (int i = 0; i < channels.Length; i++)
        channels[i] = FromProto(list.Channels[i]);
      return channels;
    }

    public static P.ChannelList ToProto(IChannel[] channels)
    {
      var list = new P.ChannelList();
      if (channels != null)
      {
        foreach (IChannel channel in channels)
          list.Channels.Add(ToProto(channel) ?? new P.Channel());
      }
      return list;
    }

    private static P.DvbBaseChannel ToProtoBase(DVBBaseChannel c)
    {
      return new P.DvbBaseChannel
               {
                 Name = S(c.Name),
                 Provider = S(c.Provider),
                 Frequency = c.Frequency,
                 NetworkId = c.NetworkId,
                 ServiceId = c.ServiceId,
                 TransportId = c.TransportId,
                 PmtPid = c.PmtPid,
                 LogicalChannelNumber = c.LogicalChannelNumber,
                 IsRadio = c.IsRadio,
                 IsTv = c.IsTv,
                 FreeToAir = c.FreeToAir
               };
    }

    private static void FromProtoBase(P.DvbBaseChannel p, DVBBaseChannel c)
    {
      if (p == null)
        return;
      c.Name = p.Name;
      c.Provider = p.Provider;
      c.Frequency = p.Frequency;
      c.NetworkId = p.NetworkId;
      c.ServiceId = p.ServiceId;
      c.TransportId = p.TransportId;
      c.PmtPid = p.PmtPid;
      c.LogicalChannelNumber = p.LogicalChannelNumber;
      c.IsRadio = p.IsRadio;
      c.IsTv = p.IsTv;
      c.FreeToAir = p.FreeToAir;
    }

    private static P.Country ToProto(Country country)
    {
      if (country == null)
        return null;
      return new P.Country
               {
                 Id = country.Id,
                 Name = S(country.Name),
                 Code = S(country.Code),
                 Index = country.Index,
                 VideoStandard = (int)country.VideoStandard
               };
    }

    private static Country FromProto(P.Country p)
    {
      if (p == null)
        return null;
      return new Country(p.Id, p.Name, p.Code, (AnalogVideoStandard)p.VideoStandard) { Index = p.Index };
    }

    #endregion

    #region Streams, genres, rtsp clients, CI menu

    public static P.AudioStream ToProto(IAudioStream stream)
    {
      if (stream == null)
        return null;
      var p = new P.AudioStream
                {
                  Language = S(stream.Language),
                  StreamType = (int)stream.StreamType,
                  Pid = stream.Pid
                };
      var analog = stream as AnalogAudioStream;
      if (analog != null)
      {
        p.IsAnalog = true;
        p.AudioMode = (int)analog.AudioMode;
      }
      return p;
    }

    public static IAudioStream FromProto(P.AudioStream p)
    {
      if (p == null)
        return null;
      if (p.IsAnalog)
        return new AnalogAudioStream
                 {
                   Language = p.Language,
                   StreamType = (AudioStreamType)p.StreamType,
                   Pid = p.Pid,
                   AudioMode = (TVAudioMode)p.AudioMode
                 };
      return new DVBAudioStream { Language = p.Language, StreamType = (AudioStreamType)p.StreamType, Pid = p.Pid };
    }

    public static P.VideoStream ToProto(IVideoStream stream)
    {
      if (stream == null)
        return null;
      return new P.VideoStream { StreamType = (int)stream.StreamType, Pid = stream.Pid, PcrPid = stream.PcrPid };
    }

    public static IVideoStream FromProto(P.VideoStream p)
    {
      if (p == null)
        return null;
      return new VideoStream { StreamType = (VideoStreamType)p.StreamType, Pid = p.Pid, PcrPid = p.PcrPid };
    }

    public static P.RtspClient ToProto(RtspClient client)
    {
      return new P.RtspClient
               {
                 IsActive = client.IsActive,
                 IpAddress = S(client.IpAdress),
                 StreamName = S(client.StreamName),
                 Description = S(client.Description),
                 DateTimeStartedTicks = client.DateTimeStarted.Ticks
               };
    }

    public static RtspClient FromProto(P.RtspClient p)
    {
      return new RtspClient(p.IsActive, p.IpAddress, p.StreamName, p.Description, new DateTime(p.DateTimeStartedTicks));
    }

    public static P.MpGenre ToProto(MpGenre genre)
    {
      var p = new P.MpGenre { Id = genre.Id, Name = S(genre.Name), IsMovie = genre.IsMovie, Enabled = genre.Enabled };
      if (genre.MappedProgramGenres != null)
        p.MappedProgramGenres.Add(genre.MappedProgramGenres);
      return p;
    }

    public static MpGenre FromProto(P.MpGenre p)
    {
      var genre = new MpGenre(p.Name, p.Id) { IsMovie = p.IsMovie, Enabled = p.Enabled };
      foreach (string programGenre in p.MappedProgramGenres)
        genre.MapToProgramGenre(programGenre);
      return genre;
    }

    public static P.CiMenu ToProto(CiMenu menu)
    {
      var p = new P.CiMenu
                {
                  Title = S(menu.Title),
                  Subtitle = S(menu.Subtitle),
                  BottomText = S(menu.BottomText),
                  State = (P.CiMenuState)menu.State,
                  NumChoices = menu.NumChoices,
                  RequestText = S(menu.RequestText),
                  AnswerLength = menu.AnswerLength,
                  Password = menu.Password
                };
      foreach (CiMenuEntry entry in menu.MenuEntries)
        p.Entries.Add(new P.CiMenuEntry { Index = entry.Index, Message = S(entry.Message) });
      return p;
    }

    public static CiMenu FromProto(P.CiMenu p)
    {
      var menu = new CiMenu(p.Title, p.Subtitle, p.BottomText, (CiMenuState)p.State) { NumChoices = p.NumChoices };
      foreach (P.CiMenuEntry entry in p.Entries)
        menu.AddEntry(entry.Index, entry.Message);
      if (!string.IsNullOrEmpty(p.RequestText) || p.AnswerLength != 0)
        menu.Request(p.RequestText, p.AnswerLength, p.Password);
      menu.State = (CiMenuState)p.State; // Request() forces "Request" - keep what the server sent
      return menu;
    }

    #endregion
  }
}
