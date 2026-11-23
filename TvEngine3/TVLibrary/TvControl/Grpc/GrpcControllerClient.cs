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
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using DirectShowLib;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using TvLibrary.Epg;
using TvLibrary.Implementations.Analog;
using TvLibrary.Interfaces;
using TvLibrary.Log;
using TvLibrary.Streaming;
using P = TvControl.Grpc.Proto;

namespace TvControl.Grpc
{
  /// <summary>
  /// <see cref="IController"/> implemented on top of the gRPC TvControlService, so existing callers
  /// of <c>RemoteControl.Instance</c> keep working unchanged.
  /// </summary>
  internal sealed class GrpcControllerClient : IController, IDisposable
  {
    private static readonly Empty E = new Empty();

    private readonly Channel _channel;
    private readonly P.TvControlService.TvControlServiceClient _client;
    private readonly Func<int> _timeoutMs;

    private readonly object _ciMenuLock = new object();
    private CiMenuCallback _ciMenuHandlers;
    private CancellationTokenSource _ciMenuCancellation;

    public GrpcControllerClient(Channel channel, Func<int> timeoutMs)
    {
      _channel = channel;
      _client = new P.TvControlService.TvControlServiceClient(channel);
      _timeoutMs = timeoutMs;
    }

    public Channel Channel
    {
      get { return _channel; }
    }

    public void Dispose()
    {
      StopCiMenuStream();
      _channel.ShutdownAsync().Wait(2000);
    }

    /// <summary>Per call deadline, same as the former remoting channel timeout.</summary>
    private CallOptions O
    {
      get
      {
        int timeout = _timeoutMs();
        return timeout > 0 ? new CallOptions(deadline: DateTime.UtcNow.AddMilliseconds(timeout)) : new CallOptions();
      }
    }

    private static P.CardRequest Card(int cardId)
    {
      return new P.CardRequest { CardId = cardId };
    }

    private static P.UserRequest U(IUser user)
    {
      return new P.UserRequest { User = ProtoMapper.ToProto(user) };
    }

    private static P.IdRequest Id(int id)
    {
      return new P.IdRequest { Id = id };
    }

    private static P.CardChannelRequest CardChannel(int cardId, IChannel channel)
    {
      return new P.CardChannelRequest { CardId = cardId, Channel = ProtoMapper.ToProto(channel) };
    }

    #region server

    public string GetAssemblyVersion
    {
      get { return _client.GetAssemblyVersion(E, O).Value; }
    }

    public int IdServer
    {
      get { return _client.GetIdServer(E, O).Value; }
    }

    public int Cards
    {
      get { return _client.GetCards(E, O).Value; }
    }

    public bool IsMaster
    {
      get { return _client.GetIsMaster(E, O).Value; }
    }

    public int ActiveStreams
    {
      get { return _client.GetActiveStreams(E, O).Value; }
    }

    public bool EpgGrabberEnabled
    {
      get { return _client.GetEpgGrabberEnabled(E, O).Value; }
      set { _client.SetEpgGrabberEnabled(new BoolValue { Value = value }, O); }
    }

    public List<string> ServerIpAdresses
    {
      get { return _client.GetServerIpAdresses(E, O).Values.ToList(); }
    }

    public int StreamingPort
    {
      get { return _client.GetStreamingPort(E, O).Value; }
    }

    public List<RtspClient> StreamingClients
    {
      get { return _client.GetStreamingClients(E, O).Clients.Select(ProtoMapper.FromProto).ToList(); }
    }

    public void ClearCache()
    {
      _client.ClearCache(E, O);
    }

    public void Restart()
    {
      _client.Restart(E, O);
    }

    public void GetDatabaseConnectionString(out string connectionString, out string provider)
    {
      P.DatabaseConnectionString reply = _client.GetDatabaseConnectionString(E, O);
      connectionString = reply.ConnectionString;
      provider = reply.Provider;
    }

    public void SetDatabaseConnectionString(string connectionString, string provider)
    {
      _client.SetDatabaseConnectionString(
        new P.DatabaseConnectionString { ConnectionString = ProtoMapper.S(connectionString), Provider = ProtoMapper.S(provider) }, O);
    }

    public void OnNewSchedule()
    {
      _client.OnNewSchedule(new P.OnNewScheduleRequest(), O);
    }

    /// <summary>
    /// The server only needs the ids of the schedules/conflicts of a TvServerEventArgs. That type lives in
    /// PluginBase (which references TvControl), so the ids are read by reflection.
    /// </summary>
    public void OnNewSchedule(EventArgs args)
    {
      var request = new P.OnNewScheduleRequest();
      if (args != null)
      {
        request.ScheduleIds.Add(ReadIds(args, "Schedules", "IdSchedule"));
        request.ConflictIds.Add(ReadIds(args, "Conflicts", "IdConflict"));
      }
      _client.OnNewSchedule(request, O);
    }

    private static IEnumerable<int> ReadIds(object args, string listProperty, string idProperty)
    {
      var property = args.GetType().GetProperty(listProperty);
      var list = property == null ? null : property.GetValue(args, null) as IEnumerable;
      if (list == null)
        yield break;
      foreach (object item in list)
      {
        var id = item == null ? null : item.GetType().GetProperty(idProperty);
        if (id != null)
          yield return (int)id.GetValue(item, null);
      }
    }

    public List<string> GetGenres()
    {
      return _client.GetGenres(E, O).Values.ToList();
    }

    public List<string> GetProgramGenres()
    {
      return _client.GetProgramGenres(E, O).Values.ToList();
    }

    public List<MpGenre> GetMpGenres()
    {
      return _client.GetMpGenres(E, O).Genres.Select(ProtoMapper.FromProto).ToList();
    }

    public List<string> GetRadioChannelGroupNames()
    {
      return _client.GetRadioChannelGroupNames(E, O).Values.ToList();
    }

    public bool IsTimeToRecord(DateTime time)
    {
      return _client.IsTimeToRecord(new P.IsTimeToRecordRequest { TimeTicks = time.Ticks }, O).Value;
    }

    public bool IsTimeToRecord(DateTime time, int scheduleId)
    {
      return _client.IsTimeToRecord(new P.IsTimeToRecordRequest { TimeTicks = time.Ticks, ScheduleId = scheduleId }, O).Value;
    }

    #endregion

    #region cards

    public bool InitConditionalAccess(int cardId)
    {
      return _client.InitConditionalAccess(Card(cardId), O).Value;
    }

    public int CardId(int cardIndex)
    {
      return _client.CardId(Id(cardIndex), O).Value;
    }

    public bool Enabled(int cardId)
    {
      return _client.Enabled(Card(cardId), O).Value;
    }

    public CardType Type(int cardId)
    {
      return (CardType)_client.Type(Card(cardId), O).Value;
    }

    public string CardName(int cardId)
    {
      return ProtoMapper.FromNullableString(_client.CardName(Card(cardId), O));
    }

    public bool CanTune(int cardId, IChannel channel)
    {
      return _client.CanTune(CardChannel(cardId, channel), O).Value;
    }

    public bool CardPresent(int cardId)
    {
      return _client.CardPresent(Card(cardId), O).Value;
    }

    public void CardRemove(int cardId)
    {
      _client.CardRemove(Card(cardId), O);
    }

    public string CardDevice(int cardId)
    {
      return ProtoMapper.FromNullableString(_client.CardDevice(Card(cardId), O));
    }

    public bool TunerLocked(int cardId)
    {
      return _client.TunerLocked(Card(cardId), O).Value;
    }

    public int SignalQuality(int cardId)
    {
      return _client.SignalQuality(Card(cardId), O).Value;
    }

    public int SignalLevel(int cardId)
    {
      return _client.SignalLevel(Card(cardId), O).Value;
    }

    public void UpdateSignalSate(int cardId)
    {
      _client.UpdateSignalSate(Card(cardId), O);
    }

    public bool IsScanning(int cardId)
    {
      return _client.IsScanning(Card(cardId), O).Value;
    }

    public bool IsGrabbingEpg(int cardId)
    {
      return _client.IsGrabbingEpg(Card(cardId), O).Value;
    }

    public IChannel[] Scan(int cardId, IChannel channel)
    {
      return ProtoMapper.FromProto(_client.Scan(CardChannel(cardId, channel), O));
    }

    public IChannel[] ScanNIT(int cardId, IChannel channel)
    {
      return ProtoMapper.FromProto(_client.ScanNIT(CardChannel(cardId, channel), O));
    }

    public int MinChannel(int cardId)
    {
      return _client.MinChannel(Card(cardId), O).Value;
    }

    public int MaxChannel(int cardId)
    {
      return _client.MaxChannel(Card(cardId), O).Value;
    }

    public int GetRecordingSchedule(int cardId, int channelId)
    {
      return _client.GetRecordingSchedule(new P.RecordingScheduleRequest { CardId = cardId, ChannelId = channelId }, O).Value;
    }

    public bool IsCardInUse(int cardId, out IUser user)
    {
      P.UserBoolReply reply = _client.IsCardInUse(Card(cardId), O);
      user = ProtoMapper.FromProto(reply.User);
      return reply.Value;
    }

    public int GetSubChannels(int idCard)
    {
      return _client.GetSubChannels(Card(idCard), O).Value;
    }

    public bool IsTunedToTransponder(int cardId, IChannel transponder)
    {
      return _client.IsTunedToTransponder(CardChannel(cardId, transponder), O).Value;
    }

    public IUser GetUserForCard(int cardId)
    {
      return ProtoMapper.FromProto(_client.GetUserForCard(Card(cardId), O).User);
    }

    public IUser[] GetUsersForCard(int cardId)
    {
      return _client.GetUsersForCard(Card(cardId), O).Users.Select(u => (IUser)ProtoMapper.FromProto(u)).ToArray();
    }

    public bool IsOwner(int cardId, IUser user)
    {
      return _client.IsOwner(new P.CardUserRequest { CardId = cardId, User = ProtoMapper.ToProto(user) }, O).Value;
    }

    public void RemoveUserFromOtherCards(int cardId, IUser user)
    {
      _client.RemoveUserFromOtherCards(new P.CardUserRequest { CardId = cardId, User = ProtoMapper.ToProto(user) }, O);
    }

    public bool SupportsSubChannels(int cardId)
    {
      return _client.SupportsSubChannels(Card(cardId), O).Value;
    }

    public int NumberOfChannelsDecrypting(int cardId)
    {
      return _client.NumberOfChannelsDecrypting(Card(cardId), O).Value;
    }

    public bool HasCA(int cardId)
    {
      return _client.HasCA(Card(cardId), O).Value;
    }

    public bool SupportsQualityControl(int cardId)
    {
      return _client.SupportsQualityControl(Card(cardId), O).Value;
    }

    public bool SupportsBitRateModes(int cardId)
    {
      return _client.SupportsBitRateModes(Card(cardId), O).Value;
    }

    public bool SupportsPeakBitRateMode(int cardId)
    {
      return _client.SupportsPeakBitRateMode(Card(cardId), O).Value;
    }

    public bool SupportsBitRate(int cardId)
    {
      return _client.SupportsBitRate(Card(cardId), O).Value;
    }

    public void ReloadCardConfiguration(int cardId)
    {
      _client.ReloadCardConfiguration(Card(cardId), O);
    }

    public QualityType GetQualityType(int cardId)
    {
      return (QualityType)_client.GetQualityType(Card(cardId), O).Value;
    }

    public void SetQualityType(int cardId, QualityType qualityType)
    {
      _client.SetQualityType(new P.CardInt32Request { CardId = cardId, Value = (int)qualityType }, O);
    }

    public VIDEOENCODER_BITRATE_MODE GetBitRateMode(int cardId)
    {
      return (VIDEOENCODER_BITRATE_MODE)_client.GetBitRateMode(Card(cardId), O).Value;
    }

    public void SetBitRateMode(int cardId, VIDEOENCODER_BITRATE_MODE bitRateMode)
    {
      _client.SetBitRateMode(new P.CardInt32Request { CardId = cardId, Value = (int)bitRateMode }, O);
    }

    #endregion

    #region DiSEqC

    public void DiSEqCReset(int cardId)
    {
      _client.DiSEqCReset(Card(cardId), O);
    }

    public void DiSEqCStopMotor(int cardId)
    {
      _client.DiSEqCStopMotor(Card(cardId), O);
    }

    public void DiSEqCSetEastLimit(int cardId)
    {
      _client.DiSEqCSetEastLimit(Card(cardId), O);
    }

    public void DiSEqCSetWestLimit(int cardId)
    {
      _client.DiSEqCSetWestLimit(Card(cardId), O);
    }

    public void DiSEqCForceLimit(int cardId, bool onoff)
    {
      _client.DiSEqCForceLimit(new P.CardBoolRequest { CardId = cardId, Value = onoff }, O);
    }

    public void DiSEqCDriveMotor(int cardId, DiSEqCDirection direction, byte numberOfSteps)
    {
      _client.DiSEqCDriveMotor(
        new P.DiSEqCDriveMotorRequest { CardId = cardId, Direction = (P.DiSEqCDirection)direction, NumberOfSteps = numberOfSteps }, O);
    }

    public void DiSEqCStorePosition(int cardId, byte position)
    {
      _client.DiSEqCStorePosition(new P.CardByteRequest { CardId = cardId, Value = position }, O);
    }

    public void DiSEqCGotoReferencePosition(int cardId)
    {
      _client.DiSEqCGotoReferencePosition(Card(cardId), O);
    }

    public void DiSEqCGotoPosition(int cardId, byte position)
    {
      _client.DiSEqCGotoPosition(new P.CardByteRequest { CardId = cardId, Value = position }, O);
    }

    public void DiSEqCGetPosition(int cardId, out int satellitePosition, out int stepsAzimuth, out int stepsElevation)
    {
      P.DiSEqCPositionReply reply = _client.DiSEqCGetPosition(Card(cardId), O);
      satellitePosition = reply.SatellitePosition;
      stepsAzimuth = reply.StepsAzimuth;
      stepsElevation = reply.StepsElevation;
    }

    #endregion

    #region CI menu

    public bool CiMenuSupported(int cardId)
    {
      return _client.CiMenuSupported(Card(cardId), O).Value;
    }

    public bool EnterCiMenu(int cardId)
    {
      return _client.EnterCiMenu(Card(cardId), O).Value;
    }

    public bool SelectMenu(int cardId, byte choice)
    {
      return _client.SelectMenu(new P.CardByteRequest { CardId = cardId, Value = choice }, O).Value;
    }

    public bool CloseMenu(int cardId)
    {
      return _client.CloseMenu(Card(cardId), O).Value;
    }

    public bool SendMenuAnswer(int cardId, bool Cancel, string Answer)
    {
      return _client.SendMenuAnswer(new P.SendMenuAnswerRequest { CardId = cardId, Cancel = Cancel, Answer = ProtoMapper.S(Answer) }, O).Value;
    }

    /// <summary>
    /// The server always routes CI callbacks through its own handler (the argument was ignored by the
    /// remoting server too); clients receive them via <see cref="OnCiMenu"/>.
    /// </summary>
    public bool SetCiMenuHandler(int cardId, ICiMenuCallbacks CallbackHandler)
    {
      return _client.SetCiMenuHandler(Card(cardId), O).Value;
    }

    /// <summary>
    /// Single subscriber, a new one replaces the previous one - same as the server-side TVController event.
    /// </summary>
    public event CiMenuCallback OnCiMenu
    {
      add
      {
        lock (_ciMenuLock)
        {
          _ciMenuHandlers = value;
          if (_ciMenuCancellation == null)
          {
            _ciMenuCancellation = new CancellationTokenSource();
            CancellationToken token = _ciMenuCancellation.Token;
            new Thread(() => ReceiveCiMenus(token)) { IsBackground = true, Name = "TvControl CI menu" }.Start();
          }
        }
      }
      remove
      {
        lock (_ciMenuLock)
        {
          _ciMenuHandlers = null;
          StopCiMenuStream();
        }
      }
    }

    private void StopCiMenuStream()
    {
      lock (_ciMenuLock)
      {
        if (_ciMenuCancellation != null)
        {
          _ciMenuCancellation.Cancel();
          _ciMenuCancellation = null;
        }
      }
    }

    private void ReceiveCiMenus(CancellationToken token)
    {
      while (!token.IsCancellationRequested)
      {
        try
        {
          using (AsyncServerStreamingCall<P.CiMenu> call = _client.SubscribeCiMenu(E, new CallOptions(cancellationToken: token)))
          {
            while (call.ResponseStream.MoveNext(token).GetAwaiter().GetResult())
            {
              CiMenuCallback handlers = _ciMenuHandlers;
              if (handlers != null)
                handlers(ProtoMapper.FromProto(call.ResponseStream.Current));
            }
          }
        }
        catch (RpcException ex) when (ex.StatusCode == StatusCode.Cancelled && token.IsCancellationRequested)
        {
          return;
        }
        catch (Exception ex)
        {
          Log.Debug("TvControl: CI menu stream interrupted, retrying - {0}", ex.Message);
        }
        token.WaitHandle.WaitOne(5000);
      }
    }

    #endregion

    #region recordings / schedules

    public byte[] GetRecordingThumbnail(string thumbnailFilename)
    {
      return ProtoMapper.FromNullableBytes(
        _client.GetRecordingThumbnail(new StringValue { Value = ProtoMapper.S(thumbnailFilename) }, O));
    }

    public string GetRecordingUrl(int idRecording)
    {
      return ProtoMapper.FromNullableString(_client.GetRecordingUrl(Id(idRecording), O));
    }

    public string GetRecordingChapters(int idRecording)
    {
      return ProtoMapper.FromNullableString(_client.GetRecordingChapters(Id(idRecording), O));
    }

    public bool DeleteRecording(int idRecording)
    {
      return _client.DeleteRecording(Id(idRecording), O).Value;
    }

    public bool IsRecordingValid(int idRecording)
    {
      return _client.IsRecordingValid(Id(idRecording), O).Value;
    }

    public bool DeleteInvalidRecordings()
    {
      return _client.DeleteInvalidRecordings(E, O).Value;
    }

    public bool DeleteWatchedRecordings(string currentTitle)
    {
      return _client.DeleteWatchedRecordings(new StringValue { Value = ProtoMapper.S(currentTitle) }, O).Value;
    }

    public string GetUrlForFile(string fileName)
    {
      return ProtoMapper.FromNullableString(_client.GetUrlForFile(new StringValue { Value = ProtoMapper.S(fileName) }, O));
    }

    public bool IsRecordingSchedule(int idSchedule, out VirtualCard card)
    {
      P.VirtualCardBoolReply reply = _client.IsRecordingSchedule(Id(idSchedule), O);
      card = ProtoMapper.FromProto(reply.Card);
      return reply.Value;
    }

    public bool IsRecording(int idChannel, out VirtualCard card)
    {
      P.VirtualCardBoolReply reply = _client.IsRecordingChannel(Id(idChannel), O);
      card = ProtoMapper.FromProto(reply.Card);
      return reply.Value;
    }

    public bool IsAnyCardRecording()
    {
      return _client.IsAnyCardRecording(E, O).Value;
    }

    public bool IsAnyCardRecordingOrTimeshifting(IUser userTS, out bool isUserTS, out bool isAnyUserTS, out bool isRec)
    {
      P.IsAnyCardRecordingOrTimeshiftingReply reply = _client.IsAnyCardRecordingOrTimeshifting(U(userTS), O);
      isUserTS = reply.IsUserTs;
      isAnyUserTS = reply.IsAnyUserTs;
      isRec = reply.IsRec;
      return reply.Value;
    }

    public bool IsAnyCardIdle()
    {
      return _client.IsAnyCardIdle(E, O).Value;
    }

    public void StopRecordingSchedule(int idSchedule)
    {
      _client.StopRecordingSchedule(Id(idSchedule), O);
    }

    public void CopyTimeShiftFile(Int64 position1, string bufferFile1, Int64 position2, string bufferFile2,
                                  string recordingFile)
    {
      _client.CopyTimeShiftFile(new P.CopyTimeShiftFileRequest
                                  {
                                    Position1 = position1,
                                    BufferFile1 = ProtoMapper.S(bufferFile1),
                                    Position2 = position2,
                                    BufferFile2 = ProtoMapper.S(bufferFile2),
                                    RecordingFile = ProtoMapper.S(recordingFile)
                                  }, O);
    }

    #endregion

    #region channel states

    public Dictionary<int, ChannelState> GetAllChannelStatesCached(IUser user)
    {
      return ProtoMapper.FromProto(_client.GetAllChannelStatesCached(U(user), O));
    }

    public Dictionary<int, ChannelState> GetAllChannelStatesForGroup(int idGroup, IUser user)
    {
      return ProtoMapper.FromProto(
        _client.GetAllChannelStatesForGroup(new P.ChannelStatesForGroupRequest { IdGroup = idGroup, User = ProtoMapper.ToProto(user) }, O));
    }

    public ChannelState GetChannelState(int idChannel, IUser user)
    {
      return (ChannelState)_client.GetChannelState(
        new P.ChannelStateRequest { IdChannel = idChannel, User = ProtoMapper.ToProto(user) }, O).Value;
    }

    public void GetAllRecordingChannels(out List<int> currentRecChannels, out List<int> currentTSChannels,
                                        out List<int> currentUnavailChannels, out List<int> currentAvailChannels)
    {
      P.AllRecordingChannelsReply reply = _client.GetAllRecordingChannels(E, O);
      currentRecChannels = reply.RecordingChannels.ToList();
      currentTSChannels = reply.TimeshiftingChannels.ToList();
      currentUnavailChannels = reply.UnavailableChannels.ToList();
      currentAvailChannels = reply.AvailableChannels.ToList();
    }

    public Dictionary<int, ChannelState> GetAllTimeshiftingAndRecordingChannels()
    {
      return ProtoMapper.FromProto(_client.GetAllTimeshiftingAndRecordingChannels(E, O));
    }

    #endregion

    #region user / timeshifting / recording

    public string GetStreamingUrl(IUser user)
    {
      return ProtoMapper.FromNullableString(_client.GetStreamingUrl(U(user), O));
    }

    private static string Value(P.UserStringReply reply, ref IUser user)
    {
      user = ProtoMapper.FromProto(reply.User);
      return reply.IsNull ? null : reply.Value;
    }

    public string RecordingFileName(ref IUser user)
    {
      return Value(_client.RecordingFileName(U(user), O), ref user);
    }

    public IChannel CurrentChannel(ref IUser user)
    {
      P.UserChannelReply reply = _client.CurrentChannel(U(user), O);
      user = ProtoMapper.FromProto(reply.User);
      return ProtoMapper.FromProto(reply.Channel);
    }

    public int CurrentDbChannel(ref IUser user)
    {
      P.UserInt32Reply reply = _client.CurrentDbChannel(U(user), O);
      user = ProtoMapper.FromProto(reply.User);
      return reply.Value;
    }

    public string CurrentChannelName(ref IUser user)
    {
      return Value(_client.CurrentChannelName(U(user), O), ref user);
    }

    private static bool Value(P.UserBoolReply reply, ref IUser user)
    {
      user = ProtoMapper.FromProto(reply.User);
      return reply.Value;
    }

    public bool IsScrambled(ref IUser user)
    {
      return Value(_client.IsScrambled(U(user), O), ref user);
    }

    public string TimeShiftFileName(ref IUser user)
    {
      return Value(_client.TimeShiftFileName(U(user), O), ref user);
    }

    public bool TimeShiftGetCurrentFilePosition(ref IUser user, ref long position, ref long bufferId)
    {
      P.TimeShiftPositionReply reply = _client.TimeShiftGetCurrentFilePosition(
        new P.TimeShiftPositionRequest { User = ProtoMapper.ToProto(user), Position = position, BufferId = bufferId }, O);
      user = ProtoMapper.FromProto(reply.User);
      position = reply.Position;
      bufferId = reply.BufferId;
      return reply.Value;
    }

    public bool IsTimeShifting(ref IUser user)
    {
      return Value(_client.IsTimeShifting(U(user), O), ref user);
    }

    public bool IsRecording(ref IUser user)
    {
      return Value(_client.IsUserRecording(U(user), O), ref user);
    }

    public DateTime TimeShiftStarted(IUser user)
    {
      return new DateTime(_client.TimeShiftStarted(U(user), O).Value);
    }

    public DateTime RecordingStarted(IUser user)
    {
      return new DateTime(_client.RecordingStarted(U(user), O).Value);
    }

    public IAudioStream[] AvailableAudioStreams(IUser user)
    {
      return _client.AvailableAudioStreams(U(user), O).Streams.Select(ProtoMapper.FromProto).ToArray();
    }

    public IAudioStream GetCurrentAudioStream(IUser user)
    {
      return ProtoMapper.FromProto(_client.GetCurrentAudioStream(U(user), O).Stream);
    }

    public void SetCurrentAudioStream(IUser user, IAudioStream stream)
    {
      _client.SetCurrentAudioStream(
        new P.SetAudioStreamRequest { User = ProtoMapper.ToProto(user), Stream = ProtoMapper.ToProto(stream) }, O);
    }

    public IVideoStream GetCurrentVideoStream(IUser user)
    {
      return ProtoMapper.FromProto(_client.GetCurrentVideoStream(U(user), O).Stream);
    }

    public TvResult StartTimeShifting(ref IUser user, ref string fileName)
    {
      P.TvResultReply reply = _client.StartTimeShiftingToFile(
        new P.UserFileRequest { User = ProtoMapper.ToProto(user), FileName = ProtoMapper.S(fileName) }, O);
      user = ProtoMapper.FromProto(reply.User);
      fileName = reply.FileName;
      return (TvResult)reply.Result;
    }

    public void StopCard(IUser user)
    {
      _client.StopCard(U(user), O);
    }

    public void PauseCard(IUser user)
    {
      _client.PauseCard(U(user), O);
    }

    public int TimeShiftingWouldUseCard(ref IUser user, int idChannel)
    {
      P.UserInt32Reply reply = _client.TimeShiftingWouldUseCard(
        new P.UserInt32Request { User = ProtoMapper.ToProto(user), Value = idChannel }, O);
      user = ProtoMapper.FromProto(reply.User);
      return reply.Value;
    }

    private static P.StartTimeShiftingChannelRequest StartRequest(IUser user, int idChannel, bool forceCardId)
    {
      return new P.StartTimeShiftingChannelRequest
               {
                 User = ProtoMapper.ToProto(user),
                 IdChannel = idChannel,
                 ForceCardId = forceCardId
               };
    }

    private static TvResult Result(P.TvResultReply reply, ref IUser user, out VirtualCard card)
    {
      user = ProtoMapper.FromProto(reply.User);
      card = ProtoMapper.FromProto(reply.Card);
      return (TvResult)reply.Result;
    }

    public TvResult StartTimeShifting(ref IUser user, int idChannel, out VirtualCard card, bool forceCardId)
    {
      return Result(_client.StartTimeShiftingChannelForce(StartRequest(user, idChannel, forceCardId), O), ref user, out card);
    }

    public TvResult StartTimeShifting(ref IUser user, int idChannel, out VirtualCard card, out bool cardChanged)
    {
      P.TvResultReply reply = _client.StartTimeShiftingChannelCardChanged(StartRequest(user, idChannel, false), O);
      cardChanged = reply.CardChanged;
      return Result(reply, ref user, out card);
    }

    public TvResult StartTimeShifting(ref IUser user, int idChannel, out VirtualCard card)
    {
      return Result(_client.StartTimeShiftingChannel(StartRequest(user, idChannel, false), O), ref user, out card);
    }

    public bool StopTimeShifting(ref IUser user, TvStoppedReason reason)
    {
      return Value(_client.StopTimeShiftingWithReason(
        new P.StopTimeShiftingRequest { User = ProtoMapper.ToProto(user), Reason = (P.TvStoppedReason)reason }, O), ref user);
    }

    public bool StopTimeShifting(ref IUser user)
    {
      return Value(_client.StopTimeShifting(U(user), O), ref user);
    }

    public TvStoppedReason GetTvStoppedReason(IUser user)
    {
      return (TvStoppedReason)_client.GetTvStoppedReason(U(user), O).Value;
    }

    public TvResult StartRecording(ref IUser user, ref string fileName)
    {
      P.TvResultReply reply = _client.StartRecording(
        new P.UserFileRequest { User = ProtoMapper.ToProto(user), FileName = ProtoMapper.S(fileName) }, O);
      user = ProtoMapper.FromProto(reply.User);
      fileName = reply.FileName;
      return (TvResult)reply.Result;
    }

    public bool StopRecording(ref IUser user)
    {
      return Value(_client.StopRecording(U(user), O), ref user);
    }

    private static P.TuneRequest TuneRequest(IUser user, IChannel channel, int idChannel)
    {
      return new P.TuneRequest { User = ProtoMapper.ToProto(user), Channel = ProtoMapper.ToProto(channel), IdChannel = idChannel };
    }

    public TvResult Scan(ref IUser user, IChannel channel, int idChannel)
    {
      P.TvResultReply reply = _client.ScanChannel(TuneRequest(user, channel, idChannel), O);
      user = ProtoMapper.FromProto(reply.User);
      return (TvResult)reply.Result;
    }

    public TvResult Tune(ref IUser user, IChannel channel, int idChannel)
    {
      P.TvResultReply reply = _client.Tune(TuneRequest(user, channel, idChannel), O);
      user = ProtoMapper.FromProto(reply.User);
      return (TvResult)reply.Result;
    }

    /// <summary>Tickets and card reservations are in-process server objects and cannot be sent over the wire.</summary>
    public TvResult Tune(ref IUser user, IChannel channel, int idChannel, object ticket)
    {
      if (ticket != null)
        throw new NotSupportedException("Card reservation tickets can only be used inside the TV server");
      return Tune(ref user, channel, idChannel);
    }

    public TvResult Tune(ref IUser user, IChannel channel, int idChannel, object ticket, object cardResImpl)
    {
      if (ticket != null || cardResImpl != null)
        throw new NotSupportedException("Card reservation tickets can only be used inside the TV server");
      return Tune(ref user, channel, idChannel);
    }

    public void HeartBeat(IUser user)
    {
      _client.HeartBeat(U(user), O);
    }

    public void GetStreamQualityCounters(IUser user, out int totalTSpackets, out int discontinuityCounter)
    {
      P.StreamQualityCountersReply reply = _client.GetStreamQualityCounters(U(user), O);
      totalTSpackets = reply.TotalTsPackets;
      discontinuityCounter = reply.DiscontinuityCounter;
    }

    #endregion

    #region teletext

    private static P.UserInt32Request UserInt(IUser user, int value)
    {
      return new P.UserInt32Request { User = ProtoMapper.ToProto(user), Value = value };
    }

    public bool IsGrabbingTeletext(IUser user)
    {
      return _client.IsGrabbingTeletext(U(user), O).Value;
    }

    public TimeSpan TeletextRotation(IUser user, int pageNumber)
    {
      return new TimeSpan(_client.TeletextRotation(UserInt(user, pageNumber), O).Value);
    }

    public bool HasTeletext(IUser user)
    {
      return _client.HasTeletext(U(user), O).Value;
    }

    public void GrabTeletext(IUser user, bool onOff)
    {
      _client.GrabTeletext(new P.UserBoolRequest { User = ProtoMapper.ToProto(user), Value = onOff }, O);
    }

    public byte[] GetTeletextPage(IUser user, int pageNumber, int subPageNumber)
    {
      return ProtoMapper.FromNullableBytes(_client.GetTeletextPage(
        new P.TeletextPageRequest { User = ProtoMapper.ToProto(user), PageNumber = pageNumber, SubPageNumber = subPageNumber }, O));
    }

    public int SubPageCount(IUser user, int pageNumber)
    {
      return _client.SubPageCount(UserInt(user, pageNumber), O).Value;
    }

    public int GetTeletextRedPageNumber(IUser user)
    {
      return _client.GetTeletextRedPageNumber(U(user), O).Value;
    }

    public int GetTeletextGreenPageNumber(IUser user)
    {
      return _client.GetTeletextGreenPageNumber(U(user), O).Value;
    }

    public int GetTeletextYellowPageNumber(IUser user)
    {
      return _client.GetTeletextYellowPageNumber(U(user), O).Value;
    }

    public int GetTeletextBluePageNumber(IUser user)
    {
      return _client.GetTeletextBluePageNumber(U(user), O).Value;
    }

    #endregion
  }
}
