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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Google.Protobuf.WellKnownTypes;
using Grpc.Core;
using TvControl;
using TvControl.Grpc;
using TvDatabase;
using TvEngine.Events;
using TvLibrary.Implementations.Analog;
using TvLibrary.Interfaces;
using TvLibrary.Log;
using P = TvControl.Grpc.Proto;
using M = TvControl.Grpc.ProtoMapper;

namespace TvService.GrpcApi
{
  /// <summary>
  /// gRPC TvControlService: a thin adapter that forwards every call to the in-process <see cref="IController"/>.
  /// </summary>
  internal sealed class TvControlServiceImpl : P.TvControlService.TvControlServiceBase
  {
    private static readonly Empty E = new Empty();
    private readonly IController _c;

    // CI menu fan-out: the controller event has a single subscriber (this), every streaming client gets a queue.
    private readonly ConcurrentDictionary<BlockingCollection<P.CiMenu>, bool> _ciMenuSubscribers =
      new ConcurrentDictionary<BlockingCollection<P.CiMenu>, bool>();

    public TvControlServiceImpl(IController controller)
    {
      _c = controller;
      _c.OnCiMenu += BroadcastCiMenu;
    }

    #region helpers

    private static Task<T> Run<T>(System.Func<T> call)
    {
      try
      {
        return Task.FromResult(call());
      }
      catch (RpcException)
      {
        throw;
      }
      catch (Exception ex)
      {
        Log.Error("TvControlService: {0}", ex);
        throw new RpcException(new Status(StatusCode.Internal, ex.GetType().Name + ": " + ex.Message));
      }
    }

    private static Task<Empty> Run(Action call)
    {
      return Run(() =>
                   {
                     call();
                     return E;
                   });
    }

    private static BoolValue B(bool value)
    {
      return new BoolValue { Value = value };
    }

    private static Int32Value I(int value)
    {
      return new Int32Value { Value = value };
    }

    private static Int64Value L(long value)
    {
      return new Int64Value { Value = value };
    }

    private static P.UserBoolReply UB(IUser user, bool value)
    {
      return new P.UserBoolReply { User = M.ToProto(user), Value = value };
    }

    private static P.UserStringReply US(IUser user, string value)
    {
      return new P.UserStringReply { User = M.ToProto(user), Value = M.S(value), IsNull = value == null };
    }

    private static P.StringList Strings(IEnumerable<string> values)
    {
      var list = new P.StringList();
      if (values != null)
        list.Values.Add(values.Select(M.S));
      return list;
    }

    #endregion

    #region server

    public override Task<StringValue> GetAssemblyVersion(Empty request, ServerCallContext context)
    {
      return Run(() => new StringValue { Value = M.S(_c.GetAssemblyVersion) });
    }

    public override Task<Int32Value> GetIdServer(Empty request, ServerCallContext context)
    {
      return Run(() => I(_c.IdServer));
    }

    public override Task<Int32Value> GetCards(Empty request, ServerCallContext context)
    {
      return Run(() => I(_c.Cards));
    }

    public override Task<BoolValue> GetIsMaster(Empty request, ServerCallContext context)
    {
      return Run(() => B(_c.IsMaster));
    }

    public override Task<Int32Value> GetActiveStreams(Empty request, ServerCallContext context)
    {
      return Run(() => I(_c.ActiveStreams));
    }

    public override Task<BoolValue> GetEpgGrabberEnabled(Empty request, ServerCallContext context)
    {
      return Run(() => B(_c.EpgGrabberEnabled));
    }

    public override Task<Empty> SetEpgGrabberEnabled(BoolValue request, ServerCallContext context)
    {
      return Run(() => { _c.EpgGrabberEnabled = request.Value; });
    }

    public override Task<P.StringList> GetServerIpAdresses(Empty request, ServerCallContext context)
    {
      return Run(() => Strings(_c.ServerIpAdresses));
    }

    public override Task<Int32Value> GetStreamingPort(Empty request, ServerCallContext context)
    {
      return Run(() => I(_c.StreamingPort));
    }

    public override Task<P.RtspClientList> GetStreamingClients(Empty request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     var list = new P.RtspClientList();
                     list.Clients.Add(_c.StreamingClients.Select(M.ToProto));
                     return list;
                   });
    }

    public override Task<Empty> ClearCache(Empty request, ServerCallContext context)
    {
      return Run(() => _c.ClearCache());
    }

    public override Task<Empty> Restart(Empty request, ServerCallContext context)
    {
      return Run(() => _c.Restart());
    }

    public override Task<P.DatabaseConnectionString> GetDatabaseConnectionString(Empty request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     string connectionString, provider;
                     _c.GetDatabaseConnectionString(out connectionString, out provider);
                     return new P.DatabaseConnectionString { ConnectionString = M.S(connectionString), Provider = M.S(provider) };
                   });
    }

    public override Task<Empty> SetDatabaseConnectionString(P.DatabaseConnectionString request, ServerCallContext context)
    {
      return Run(() => _c.SetDatabaseConnectionString(request.ConnectionString, request.Provider));
    }

    public override Task<Empty> OnNewSchedule(P.OnNewScheduleRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     if (request.ScheduleIds.Count == 0 && request.ConflictIds.Count == 0)
                     {
                       _c.OnNewSchedule();
                       return;
                     }
                     IList<Schedule> schedules = request.ScheduleIds.Select(Schedule.Retrieve).Where(s => s != null).ToList();
                     IList<Conflict> conflicts = request.ConflictIds.Select(Conflict.Retrieve).Where(c => c != null).ToList();
                     _c.OnNewSchedule(new TvServerEventArgs(TvServerEventType.ScheduledAdded, schedules, conflicts, null));
                   });
    }

    public override Task<P.StringList> GetGenres(Empty request, ServerCallContext context)
    {
      return Run(() => Strings(_c.GetGenres()));
    }

    public override Task<P.StringList> GetProgramGenres(Empty request, ServerCallContext context)
    {
      return Run(() => Strings(_c.GetProgramGenres()));
    }

    public override Task<P.MpGenreList> GetMpGenres(Empty request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     var list = new P.MpGenreList();
                     list.Genres.Add(_c.GetMpGenres().Select(M.ToProto));
                     return list;
                   });
    }

    public override Task<P.StringList> GetRadioChannelGroupNames(Empty request, ServerCallContext context)
    {
      return Run(() => Strings(_c.GetRadioChannelGroupNames()));
    }

    public override Task<BoolValue> IsTimeToRecord(P.IsTimeToRecordRequest request, ServerCallContext context)
    {
      var time = new DateTime(request.TimeTicks);
      return Run(() => B(request.HasScheduleId ? _c.IsTimeToRecord(time, request.ScheduleId) : _c.IsTimeToRecord(time)));
    }

    #endregion

    #region cards

    public override Task<BoolValue> InitConditionalAccess(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.InitConditionalAccess(request.CardId)));
    }

    public override Task<Int32Value> CardId(P.IdRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.CardId(request.Id)));
    }

    public override Task<BoolValue> Enabled(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.Enabled(request.CardId)));
    }

    public override Task<P.CardTypeReply> Type(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => new P.CardTypeReply { Value = (P.CardType)_c.Type(request.CardId) });
    }

    public override Task<P.NullableStringReply> CardName(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => M.ToNullableString(_c.CardName(request.CardId)));
    }

    public override Task<BoolValue> CanTune(P.CardChannelRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.CanTune(request.CardId, M.FromProto(request.Channel))));
    }

    public override Task<BoolValue> CardPresent(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.CardPresent(request.CardId)));
    }

    public override Task<Empty> CardRemove(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.CardRemove(request.CardId));
    }

    public override Task<P.NullableStringReply> CardDevice(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => M.ToNullableString(_c.CardDevice(request.CardId)));
    }

    public override Task<BoolValue> TunerLocked(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.TunerLocked(request.CardId)));
    }

    public override Task<Int32Value> SignalQuality(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.SignalQuality(request.CardId)));
    }

    public override Task<Int32Value> SignalLevel(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.SignalLevel(request.CardId)));
    }

    public override Task<Empty> UpdateSignalSate(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.UpdateSignalSate(request.CardId));
    }

    public override Task<BoolValue> IsScanning(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.IsScanning(request.CardId)));
    }

    public override Task<BoolValue> IsGrabbingEpg(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.IsGrabbingEpg(request.CardId)));
    }

    public override Task<P.ChannelList> Scan(P.CardChannelRequest request, ServerCallContext context)
    {
      return Run(() => M.ToProto(_c.Scan(request.CardId, M.FromProto(request.Channel))));
    }

    public override Task<P.ChannelList> ScanNIT(P.CardChannelRequest request, ServerCallContext context)
    {
      return Run(() => M.ToProto(_c.ScanNIT(request.CardId, M.FromProto(request.Channel))));
    }

    public override Task<Int32Value> MinChannel(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.MinChannel(request.CardId)));
    }

    public override Task<Int32Value> MaxChannel(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.MaxChannel(request.CardId)));
    }

    public override Task<Int32Value> GetRecordingSchedule(P.RecordingScheduleRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.GetRecordingSchedule(request.CardId, request.ChannelId)));
    }

    public override Task<P.UserBoolReply> IsCardInUse(P.CardRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user;
                     bool value = _c.IsCardInUse(request.CardId, out user);
                     return UB(user, value);
                   });
    }

    public override Task<Int32Value> GetSubChannels(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.GetSubChannels(request.CardId)));
    }

    public override Task<BoolValue> IsTunedToTransponder(P.CardChannelRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.IsTunedToTransponder(request.CardId, M.FromProto(request.Channel))));
    }

    public override Task<P.UserReply> GetUserForCard(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => new P.UserReply { User = M.ToProto(_c.GetUserForCard(request.CardId)) });
    }

    public override Task<P.UserList> GetUsersForCard(P.CardRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     var list = new P.UserList();
                     IUser[] users = _c.GetUsersForCard(request.CardId);
                     if (users != null)
                       list.Users.Add(users.Where(u => u != null).Select(M.ToProto));
                     return list;
                   });
    }

    public override Task<BoolValue> IsOwner(P.CardUserRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.IsOwner(request.CardId, M.FromProto(request.User))));
    }

    public override Task<Empty> RemoveUserFromOtherCards(P.CardUserRequest request, ServerCallContext context)
    {
      return Run(() => _c.RemoveUserFromOtherCards(request.CardId, M.FromProto(request.User)));
    }

    public override Task<BoolValue> SupportsSubChannels(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SupportsSubChannels(request.CardId)));
    }

    public override Task<Int32Value> NumberOfChannelsDecrypting(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.NumberOfChannelsDecrypting(request.CardId)));
    }

    public override Task<BoolValue> HasCA(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.HasCA(request.CardId)));
    }

    public override Task<BoolValue> SupportsQualityControl(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SupportsQualityControl(request.CardId)));
    }

    public override Task<BoolValue> SupportsBitRateModes(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SupportsBitRateModes(request.CardId)));
    }

    public override Task<BoolValue> SupportsPeakBitRateMode(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SupportsPeakBitRateMode(request.CardId)));
    }

    public override Task<BoolValue> SupportsBitRate(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SupportsBitRate(request.CardId)));
    }

    public override Task<Empty> ReloadCardConfiguration(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.ReloadCardConfiguration(request.CardId));
    }

    public override Task<Int32Value> GetQualityType(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I((int)_c.GetQualityType(request.CardId)));
    }

    public override Task<Empty> SetQualityType(P.CardInt32Request request, ServerCallContext context)
    {
      return Run(() => _c.SetQualityType(request.CardId, (QualityType)request.Value));
    }

    public override Task<Int32Value> GetBitRateMode(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => I((int)_c.GetBitRateMode(request.CardId)));
    }

    public override Task<Empty> SetBitRateMode(P.CardInt32Request request, ServerCallContext context)
    {
      return Run(() => _c.SetBitRateMode(request.CardId, (VIDEOENCODER_BITRATE_MODE)request.Value));
    }

    #endregion

    #region DiSEqC

    public override Task<Empty> DiSEqCReset(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCReset(request.CardId));
    }

    public override Task<Empty> DiSEqCStopMotor(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCStopMotor(request.CardId));
    }

    public override Task<Empty> DiSEqCSetEastLimit(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCSetEastLimit(request.CardId));
    }

    public override Task<Empty> DiSEqCSetWestLimit(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCSetWestLimit(request.CardId));
    }

    public override Task<Empty> DiSEqCForceLimit(P.CardBoolRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCForceLimit(request.CardId, request.Value));
    }

    public override Task<Empty> DiSEqCDriveMotor(P.DiSEqCDriveMotorRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCDriveMotor(request.CardId, (DiSEqCDirection)request.Direction, ToByte(request.NumberOfSteps)));
    }

    public override Task<Empty> DiSEqCStorePosition(P.CardByteRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCStorePosition(request.CardId, ToByte(request.Value)));
    }

    public override Task<Empty> DiSEqCGotoReferencePosition(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCGotoReferencePosition(request.CardId));
    }

    public override Task<Empty> DiSEqCGotoPosition(P.CardByteRequest request, ServerCallContext context)
    {
      return Run(() => _c.DiSEqCGotoPosition(request.CardId, ToByte(request.Value)));
    }

    public override Task<P.DiSEqCPositionReply> DiSEqCGetPosition(P.CardRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     int satellitePosition, stepsAzimuth, stepsElevation;
                     _c.DiSEqCGetPosition(request.CardId, out satellitePosition, out stepsAzimuth, out stepsElevation);
                     return new P.DiSEqCPositionReply
                              {
                                SatellitePosition = satellitePosition,
                                StepsAzimuth = stepsAzimuth,
                                StepsElevation = stepsElevation
                              };
                   });
    }

    private static byte ToByte(uint value)
    {
      if (value > byte.MaxValue)
        throw new RpcException(new Status(StatusCode.InvalidArgument, "Value out of range: " + value));
      return (byte)value;
    }

    #endregion

    #region CI menu

    public override Task<BoolValue> CiMenuSupported(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.CiMenuSupported(request.CardId)));
    }

    public override Task<BoolValue> EnterCiMenu(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.EnterCiMenu(request.CardId)));
    }

    public override Task<BoolValue> SelectMenu(P.CardByteRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SelectMenu(request.CardId, ToByte(request.Value))));
    }

    public override Task<BoolValue> CloseMenu(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.CloseMenu(request.CardId)));
    }

    public override Task<BoolValue> SendMenuAnswer(P.SendMenuAnswerRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SendMenuAnswer(request.CardId, request.Cancel, request.Answer)));
    }

    public override Task<BoolValue> SetCiMenuHandler(P.CardRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.SetCiMenuHandler(request.CardId, null)));
    }

    private void BroadcastCiMenu(CiMenu menu)
    {
      P.CiMenu message;
      try
      {
        message = M.ToProto(menu);
      }
      catch (Exception ex)
      {
        Log.Error("TvControlService: cannot forward CI menu - {0}", ex);
        return;
      }
      foreach (BlockingCollection<P.CiMenu> queue in _ciMenuSubscribers.Keys)
        queue.TryAdd(message);
    }

    public override async Task SubscribeCiMenu(Empty request, IServerStreamWriter<P.CiMenu> responseStream,
                                               ServerCallContext context)
    {
      using (var queue = new BlockingCollection<P.CiMenu>(new ConcurrentQueue<P.CiMenu>(), 100))
      {
        _ciMenuSubscribers[queue] = true;
        try
        {
          while (!context.CancellationToken.IsCancellationRequested)
          {
            P.CiMenu menu;
            try
            {
              menu = await Task.Run(() => queue.Take(context.CancellationToken)).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
              break;
            }
            await responseStream.WriteAsync(menu).ConfigureAwait(false);
          }
        }
        finally
        {
          bool ignored;
          _ciMenuSubscribers.TryRemove(queue, out ignored);
        }
      }
    }

    #endregion

    #region recordings / schedules

    public override Task<P.NullableBytesReply> GetRecordingThumbnail(StringValue request, ServerCallContext context)
    {
      return Run(() => M.ToNullableBytes(_c.GetRecordingThumbnail(request.Value)));
    }

    public override Task<P.NullableStringReply> GetRecordingUrl(P.IdRequest request, ServerCallContext context)
    {
      return Run(() => M.ToNullableString(_c.GetRecordingUrl(request.Id)));
    }

    public override Task<P.NullableStringReply> GetRecordingChapters(P.IdRequest request, ServerCallContext context)
    {
      return Run(() => M.ToNullableString(_c.GetRecordingChapters(request.Id)));
    }

    public override Task<BoolValue> DeleteRecording(P.IdRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.DeleteRecording(request.Id)));
    }

    public override Task<BoolValue> IsRecordingValid(P.IdRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.IsRecordingValid(request.Id)));
    }

    public override Task<BoolValue> DeleteInvalidRecordings(Empty request, ServerCallContext context)
    {
      return Run(() => B(_c.DeleteInvalidRecordings()));
    }

    public override Task<BoolValue> DeleteWatchedRecordings(StringValue request, ServerCallContext context)
    {
      return Run(() => B(_c.DeleteWatchedRecordings(request.Value)));
    }

    public override Task<P.NullableStringReply> GetUrlForFile(StringValue request, ServerCallContext context)
    {
      return Run(() => M.ToNullableString(_c.GetUrlForFile(request.Value)));
    }

    public override Task<P.VirtualCardBoolReply> IsRecordingSchedule(P.IdRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     VirtualCard card;
                     bool value = _c.IsRecordingSchedule(request.Id, out card);
                     return new P.VirtualCardBoolReply { Value = value, Card = M.ToProto(card) };
                   });
    }

    public override Task<P.VirtualCardBoolReply> IsRecordingChannel(P.IdRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     VirtualCard card;
                     bool value = _c.IsRecording(request.Id, out card);
                     return new P.VirtualCardBoolReply { Value = value, Card = M.ToProto(card) };
                   });
    }

    public override Task<BoolValue> IsAnyCardRecording(Empty request, ServerCallContext context)
    {
      return Run(() => B(_c.IsAnyCardRecording()));
    }

    public override Task<P.IsAnyCardRecordingOrTimeshiftingReply> IsAnyCardRecordingOrTimeshifting(P.UserRequest request,
                                                                                                    ServerCallContext context)
    {
      return Run(() =>
                   {
                     bool isUserTs, isAnyUserTs, isRec;
                     bool value = _c.IsAnyCardRecordingOrTimeshifting(M.FromProto(request.User), out isUserTs, out isAnyUserTs,
                                                                      out isRec);
                     return new P.IsAnyCardRecordingOrTimeshiftingReply
                              {
                                Value = value,
                                IsUserTs = isUserTs,
                                IsAnyUserTs = isAnyUserTs,
                                IsRec = isRec
                              };
                   });
    }

    public override Task<BoolValue> IsAnyCardIdle(Empty request, ServerCallContext context)
    {
      return Run(() => B(_c.IsAnyCardIdle()));
    }

    public override Task<Empty> StopRecordingSchedule(P.IdRequest request, ServerCallContext context)
    {
      return Run(() => _c.StopRecordingSchedule(request.Id));
    }

    public override Task<Empty> CopyTimeShiftFile(P.CopyTimeShiftFileRequest request, ServerCallContext context)
    {
      return Run(() => _c.CopyTimeShiftFile(request.Position1, request.BufferFile1, request.Position2, request.BufferFile2,
                                            request.RecordingFile));
    }

    #endregion

    #region channel states

    public override Task<P.ChannelStateMap> GetAllChannelStatesCached(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => M.ToProto(_c.GetAllChannelStatesCached(M.FromProto(request.User))));
    }

    public override Task<P.ChannelStateMap> GetAllChannelStatesForGroup(P.ChannelStatesForGroupRequest request,
                                                                        ServerCallContext context)
    {
      return Run(() => M.ToProto(_c.GetAllChannelStatesForGroup(request.IdGroup, M.FromProto(request.User))));
    }

    public override Task<P.ChannelStateReply> GetChannelState(P.ChannelStateRequest request, ServerCallContext context)
    {
      return Run(() => new P.ChannelStateReply
                         {
                           Value = (P.ChannelState)_c.GetChannelState(request.IdChannel, M.FromProto(request.User))
                         });
    }

    public override Task<P.AllRecordingChannelsReply> GetAllRecordingChannels(Empty request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     List<int> rec, ts, unavailable, available;
                     _c.GetAllRecordingChannels(out rec, out ts, out unavailable, out available);
                     var reply = new P.AllRecordingChannelsReply();
                     if (rec != null) reply.RecordingChannels.Add(rec);
                     if (ts != null) reply.TimeshiftingChannels.Add(ts);
                     if (unavailable != null) reply.UnavailableChannels.Add(unavailable);
                     if (available != null) reply.AvailableChannels.Add(available);
                     return reply;
                   });
    }

    public override Task<P.ChannelStateMap> GetAllTimeshiftingAndRecordingChannels(Empty request, ServerCallContext context)
    {
      return Run(() => M.ToProto(_c.GetAllTimeshiftingAndRecordingChannels()));
    }

    #endregion

    #region user / timeshifting / recording

    public override Task<P.NullableStringReply> GetStreamingUrl(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => M.ToNullableString(_c.GetStreamingUrl(M.FromProto(request.User))));
    }

    public override Task<P.UserStringReply> RecordingFileName(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     string value = _c.RecordingFileName(ref user);
                     return US(user, value);
                   });
    }

    public override Task<P.UserChannelReply> CurrentChannel(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     IChannel channel = _c.CurrentChannel(ref user);
                     return new P.UserChannelReply { User = M.ToProto(user), Channel = M.ToProto(channel) };
                   });
    }

    public override Task<P.UserInt32Reply> CurrentDbChannel(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     int value = _c.CurrentDbChannel(ref user);
                     return new P.UserInt32Reply { User = M.ToProto(user), Value = value };
                   });
    }

    public override Task<P.UserStringReply> CurrentChannelName(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     string value = _c.CurrentChannelName(ref user);
                     return US(user, value);
                   });
    }

    public override Task<P.UserBoolReply> IsScrambled(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     bool value = _c.IsScrambled(ref user);
                     return UB(user, value);
                   });
    }

    public override Task<P.UserStringReply> TimeShiftFileName(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     string value = _c.TimeShiftFileName(ref user);
                     return US(user, value);
                   });
    }

    public override Task<P.TimeShiftPositionReply> TimeShiftGetCurrentFilePosition(P.TimeShiftPositionRequest request,
                                                                                    ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     long position = request.Position;
                     long bufferId = request.BufferId;
                     bool value = _c.TimeShiftGetCurrentFilePosition(ref user, ref position, ref bufferId);
                     return new P.TimeShiftPositionReply
                              {
                                User = M.ToProto(user),
                                Value = value,
                                Position = position,
                                BufferId = bufferId
                              };
                   });
    }

    public override Task<P.UserBoolReply> IsTimeShifting(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     bool value = _c.IsTimeShifting(ref user);
                     return UB(user, value);
                   });
    }

    public override Task<P.UserBoolReply> IsUserRecording(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     bool value = _c.IsRecording(ref user);
                     return UB(user, value);
                   });
    }

    public override Task<Int64Value> TimeShiftStarted(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => L(_c.TimeShiftStarted(M.FromProto(request.User)).Ticks));
    }

    public override Task<Int64Value> RecordingStarted(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => L(_c.RecordingStarted(M.FromProto(request.User)).Ticks));
    }

    public override Task<P.AudioStreamList> AvailableAudioStreams(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     var list = new P.AudioStreamList();
                     IAudioStream[] streams = _c.AvailableAudioStreams(M.FromProto(request.User));
                     if (streams != null)
                       list.Streams.Add(streams.Where(s => s != null).Select(M.ToProto));
                     return list;
                   });
    }

    public override Task<P.AudioStreamReply> GetCurrentAudioStream(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => new P.AudioStreamReply { Stream = M.ToProto(_c.GetCurrentAudioStream(M.FromProto(request.User))) });
    }

    public override Task<Empty> SetCurrentAudioStream(P.SetAudioStreamRequest request, ServerCallContext context)
    {
      return Run(() => _c.SetCurrentAudioStream(M.FromProto(request.User), M.FromProto(request.Stream)));
    }

    public override Task<P.VideoStreamReply> GetCurrentVideoStream(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => new P.VideoStreamReply { Stream = M.ToProto(_c.GetCurrentVideoStream(M.FromProto(request.User))) });
    }

    public override Task<P.TvResultReply> StartTimeShiftingToFile(P.UserFileRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     string fileName = request.FileName;
                     TvResult result = _c.StartTimeShifting(ref user, ref fileName);
                     return new P.TvResultReply { Result = (P.TvResult)result, User = M.ToProto(user), FileName = M.S(fileName) };
                   });
    }

    public override Task<Empty> StopCard(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => _c.StopCard(M.FromProto(request.User)));
    }

    public override Task<Empty> PauseCard(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => _c.PauseCard(M.FromProto(request.User)));
    }

    public override Task<P.UserInt32Reply> TimeShiftingWouldUseCard(P.UserInt32Request request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     int value = _c.TimeShiftingWouldUseCard(ref user, request.Value);
                     return new P.UserInt32Reply { User = M.ToProto(user), Value = value };
                   });
    }

    private static P.TvResultReply Reply(TvResult result, IUser user, VirtualCard card)
    {
      return new P.TvResultReply { Result = (P.TvResult)result, User = M.ToProto(user), Card = M.ToProto(card) };
    }

    public override Task<P.TvResultReply> StartTimeShiftingChannel(P.StartTimeShiftingChannelRequest request,
                                                                   ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     VirtualCard card;
                     TvResult result = _c.StartTimeShifting(ref user, request.IdChannel, out card);
                     return Reply(result, user, card);
                   });
    }

    public override Task<P.TvResultReply> StartTimeShiftingChannelForce(P.StartTimeShiftingChannelRequest request,
                                                                        ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     VirtualCard card;
                     TvResult result = _c.StartTimeShifting(ref user, request.IdChannel, out card, request.ForceCardId);
                     return Reply(result, user, card);
                   });
    }

    public override Task<P.TvResultReply> StartTimeShiftingChannelCardChanged(P.StartTimeShiftingChannelRequest request,
                                                                              ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     VirtualCard card;
                     bool cardChanged;
                     TvResult result = _c.StartTimeShifting(ref user, request.IdChannel, out card, out cardChanged);
                     P.TvResultReply reply = Reply(result, user, card);
                     reply.CardChanged = cardChanged;
                     return reply;
                   });
    }

    public override Task<P.UserBoolReply> StopTimeShifting(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     bool value = _c.StopTimeShifting(ref user);
                     return UB(user, value);
                   });
    }

    public override Task<P.UserBoolReply> StopTimeShiftingWithReason(P.StopTimeShiftingRequest request,
                                                                      ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     bool value = _c.StopTimeShifting(ref user, (TvStoppedReason)request.Reason);
                     return UB(user, value);
                   });
    }

    public override Task<P.TvStoppedReasonReply> GetTvStoppedReason(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => new P.TvStoppedReasonReply
                         {
                           Value = (P.TvStoppedReason)_c.GetTvStoppedReason(M.FromProto(request.User))
                         });
    }

    public override Task<P.TvResultReply> StartRecording(P.UserFileRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     string fileName = request.FileName;
                     TvResult result = _c.StartRecording(ref user, ref fileName);
                     return new P.TvResultReply { Result = (P.TvResult)result, User = M.ToProto(user), FileName = M.S(fileName) };
                   });
    }

    public override Task<P.UserBoolReply> StopRecording(P.UserRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     bool value = _c.StopRecording(ref user);
                     return UB(user, value);
                   });
    }

    public override Task<P.TvResultReply> ScanChannel(P.TuneRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     TvResult result = _c.Scan(ref user, M.FromProto(request.Channel), request.IdChannel);
                     return Reply(result, user, null);
                   });
    }

    public override Task<P.TvResultReply> Tune(P.TuneRequest request, ServerCallContext context)
    {
      return Run(() =>
                   {
                     IUser user = M.FromProto(request.User);
                     TvResult result = _c.Tune(ref user, M.FromProto(request.Channel), request.IdChannel);
                     return Reply(result, user, null);
                   });
    }

    public override Task<Empty> HeartBeat(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => _c.HeartBeat(M.FromProto(request.User)));
    }

    public override Task<P.StreamQualityCountersReply> GetStreamQualityCounters(P.UserRequest request,
                                                                                ServerCallContext context)
    {
      return Run(() =>
                   {
                     int totalTsPackets, discontinuityCounter;
                     _c.GetStreamQualityCounters(M.FromProto(request.User), out totalTsPackets, out discontinuityCounter);
                     return new P.StreamQualityCountersReply
                              {
                                TotalTsPackets = totalTsPackets,
                                DiscontinuityCounter = discontinuityCounter
                              };
                   });
    }

    #endregion

    #region teletext

    public override Task<BoolValue> IsGrabbingTeletext(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.IsGrabbingTeletext(M.FromProto(request.User))));
    }

    public override Task<Int64Value> TeletextRotation(P.UserInt32Request request, ServerCallContext context)
    {
      return Run(() => L(_c.TeletextRotation(M.FromProto(request.User), request.Value).Ticks));
    }

    public override Task<BoolValue> HasTeletext(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => B(_c.HasTeletext(M.FromProto(request.User))));
    }

    public override Task<Empty> GrabTeletext(P.UserBoolRequest request, ServerCallContext context)
    {
      return Run(() => _c.GrabTeletext(M.FromProto(request.User), request.Value));
    }

    public override Task<P.NullableBytesReply> GetTeletextPage(P.TeletextPageRequest request, ServerCallContext context)
    {
      return Run(() => M.ToNullableBytes(_c.GetTeletextPage(M.FromProto(request.User), request.PageNumber,
                                                            request.SubPageNumber)));
    }

    public override Task<Int32Value> SubPageCount(P.UserInt32Request request, ServerCallContext context)
    {
      return Run(() => I(_c.SubPageCount(M.FromProto(request.User), request.Value)));
    }

    public override Task<Int32Value> GetTeletextRedPageNumber(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.GetTeletextRedPageNumber(M.FromProto(request.User))));
    }

    public override Task<Int32Value> GetTeletextGreenPageNumber(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.GetTeletextGreenPageNumber(M.FromProto(request.User))));
    }

    public override Task<Int32Value> GetTeletextYellowPageNumber(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.GetTeletextYellowPageNumber(M.FromProto(request.User))));
    }

    public override Task<Int32Value> GetTeletextBluePageNumber(P.UserRequest request, ServerCallContext context)
    {
      return Run(() => I(_c.GetTeletextBluePageNumber(M.FromProto(request.User))));
    }

    #endregion
  }
}
