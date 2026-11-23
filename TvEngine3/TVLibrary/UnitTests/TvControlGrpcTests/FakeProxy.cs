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
using System.Reflection;
using System.Runtime.Remoting.Messaging;
using System.Runtime.Remoting.Proxies;

namespace TvControlGrpcTests
{
  /// <summary>
  /// In-process fake of a large interface (IController has 150 members): unscripted members return defaults.
  /// (RealProxy is only used as a test double here, nothing goes over the network through it.)
  /// </summary>
  internal sealed class FakeProxy<T> : RealProxy where T : class
  {
    /// <summary>Handlers keyed by "MethodName/ArgCount"; they may assign ref/out values into the args array.</summary>
    public readonly Dictionary<string, Func<object[], object>> Handlers = new Dictionary<string, Func<object[], object>>();

    /// <summary>Last handler added to each event, keyed by event name.</summary>
    public readonly Dictionary<string, Delegate> Subscribers = new Dictionary<string, Delegate>();

    public FakeProxy() : base(typeof(T)) {}

    public T Instance
    {
      get { return (T)GetTransparentProxy(); }
    }

    public override IMessage Invoke(IMessage msg)
    {
      var call = (IMethodCallMessage)msg;
      object[] args = (object[])call.Args.Clone();

      if (call.MethodName.StartsWith("add_"))
        Subscribers[call.MethodName.Substring(4)] = (Delegate)args[0];
      else if (call.MethodName.StartsWith("remove_"))
        Subscribers.Remove(call.MethodName.Substring(7));

      Func<object[], object> handler;
      Type returnType = ((MethodInfo)call.MethodBase).ReturnType;
      object result = Handlers.TryGetValue(call.MethodName + "/" + args.Length, out handler)
                        ? handler(args)
                        : (returnType.IsValueType && returnType != typeof(void) ? Activator.CreateInstance(returnType) : null);
      return new ReturnMessage(result, args, args.Length, call.LogicalCallContext, call);
    }
  }
}
