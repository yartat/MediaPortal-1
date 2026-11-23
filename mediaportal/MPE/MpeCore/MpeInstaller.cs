#region Copyright (C) 2005-2011 Team MediaPortal

// Copyright (C) 2005-2011 Team MediaPortal
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
using MpeCore.Classes.ProviderHelpers;
using MpeCore.Interfaces;
using MpeCore.Classes;
using MpeCore.Classes.InstallerType;
using MpeCore.Classes.ActionType;
using MpeCore.Classes.PathProvider;
using MpeCore.Classes.SectionPanel;
using MpeCore.Classes.ZipProvider;
using MpeCore.Classes.VersionProvider;

namespace MpeCore
{
  public static class MpeInstaller
  {
    public static Dictionary<string, IInstallerTypeProvider> InstallerTypeProviders { get; set; }
    public static Dictionary<string, IPathProvider> PathProviders { get; set; }
    public static SectionProviderHelper SectionPanels { get; set; }
    public static Dictionary<string, IActionType> ActionProviders { get; set; }
    public static Dictionary<string, IVersionProvider> VersionProviders { get; set; }
    public static ZipProviderClass ZipProvider { get; set; }
    public static ExtensionCollection InstalledExtensions { get; set; }
    public static ExtensionCollection KnownExtensions { get; set; }

    private static List<string> extensionUrlList = new List<string>();

    public static void Init()
    {
      InstallerTypeProviders = new Dictionary<string, IInstallerTypeProvider>();
      PathProviders = new Dictionary<string, IPathProvider>();
      SectionPanels = new SectionProviderHelper();
      ActionProviders = new Dictionary<string, IActionType>();
      VersionProviders = new Dictionary<string, IVersionProvider>();
      ZipProvider = new ZipProviderClass();


      AddInstallType(new CopyFile());
      AddInstallType(new CopyFont());
      AddInstallType(new GenericSkinFile());

      PathProviders.Add("MediaPortalPaths", new MediaPortalPaths());
      PathProviders.Add("TvServerPaths", new TvServerPaths());
      PathProviders.Add("WindowsPaths", new WindowsPaths());

      AddSection(new Welcome());
      AddSection(new LicenseAgreement());
      AddSection(new ReadmeInformation());
      AddSection(new ImageRadioSelector());
      AddSection(new TreeViewSelector());
      AddSection(new InstallSection());
      AddSection(new Finish());
      AddSection(new GroupCheck());
      AddSection(new GroupCheckScript());

      AddActionProvider(new InstallFiles());
      AddActionProvider(new ShowMessageBox());
      AddActionProvider(new ClearSkinCache());
      AddActionProvider(new RunApplication());
      AddActionProvider(new KillTask());
      AddActionProvider(new CreateShortCut());
      AddActionProvider(new CreateFolder());
      AddActionProvider(new ExtensionInstaller());
      AddActionProvider(new ConfigurePlugin());
      AddActionProvider(new Script());

      AddVersion(new MediaPortalVersion());
      AddVersion(new SkinVersion());
      AddVersion(new TvServerVersion());
      AddVersion(new ExtensionVersion());
      AddVersion(new InstallerVersion());

      InstalledExtensions =
        ExtensionCollection.Load(string.Format("{0}\\InstalledExtensions.xml", BaseFolder));
      KnownExtensions =
        ExtensionCollection.Load(string.Format("{0}\\KnownExtensions.xml", BaseFolder));
    }

    public static void AddVersion(IVersionProvider provider)
    {
      VersionProviders.Add(provider.DisplayName, provider);
    }

    public static void AddSection(ISectionPanel sp)
    {
      SectionPanels.Add(sp.DisplayName, sp);
    }

    public static void AddInstallType(IInstallerTypeProvider provider)
    {
      InstallerTypeProviders.Add(provider.Name, provider);
    }

    public static void AddActionProvider(IActionType ac)
    {
      ActionProviders.Add(ac.DisplayName, ac);
    }

    public static void Save()
    {
      if (!Directory.Exists(BaseFolder))
        Directory.CreateDirectory(BaseFolder);
      InstalledExtensions.Save(string.Format("{0}\\InstalledExtensions.xml", BaseFolder));
      KnownExtensions.Save(string.Format("{0}\\KnownExtensions.xml", BaseFolder));
    }

    /// <summary>
    /// Gets the folder were the installation information are store .
    /// Same like %Installer%
    /// </summary>
    /// <value>The base folder.</value>
    public static string BaseFolder
    {
      get { return string.Format("{0}\\V2", Util.InstallerConfigDir); }
    }

    /// <summary>
    /// Transfor a real path in a template path, based on providers
    /// </summary>
    /// <param name="localFile">The location of file.</param>
    /// <returns></returns>
    public static string TransformInTemplatePath(string localFile)
    {
      foreach (var pathProvider in PathProviders)
      {
        localFile = pathProvider.Value.Colapse(localFile);
      }
      return localFile;
    }

    /// <summary>
    /// Transfor a template path in a real system path path, based on providers
    /// </summary>
    /// <param name="localFile">The template of file or path.</param>
    /// <returns></returns>
    /// <summary>
    /// Windows folders an extension may not install into, even through a path template. The folders of
    /// MediaPortal and the TV server, the fonts folder and the data folders of the user stay allowed.
    /// </summary>
    private static readonly string[] ForbiddenWindowsFolders =
      {
        "%Windows%", "%System%", "%SystemX86%", "%ProgramFiles%", "%ProgramFilesX86%", "%CommonProgramFiles%",
        "%CommonProgramFilesX86%", "%Startup%", "%CommonStartup%"
      };

    /// <summary>
    /// Checks that a file is written below one of the known folders (MediaPortal, TV server, fonts, user data).
    /// A destination like "%Base%\..\..\Windows\x.dll" or "C:\Windows\x.dll" is refused.
    /// </summary>
    public static bool IsKnownInstallLocation(string path)
    {
      string full = Path.GetFullPath(path);
      // The startup folders lie below the start menu folder, which is allowed (shortcuts), but they run programs at logon.
      foreach (Environment.SpecialFolder startup in new[] { Environment.SpecialFolder.Startup, Environment.SpecialFolder.CommonStartup })
      {
        string startupFolder = Environment.GetFolderPath(startup);
        if (!string.IsNullOrEmpty(startupFolder) &&
            full.StartsWith(Path.GetFullPath(startupFolder) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
        {
          return false;
        }
      }
      foreach (var pathProvider in PathProviders)
      {
        foreach (KeyValuePair<string, string> folder in pathProvider.Value.Paths)
        {
          if (string.IsNullOrEmpty(folder.Value) ||
              (pathProvider.Key == "WindowsPaths" && Array.IndexOf(ForbiddenWindowsFolders, folder.Key) >= 0))
          {
            continue;
          }
          string root = Path.GetFullPath(folder.Value);
          if (!root.EndsWith(Path.DirectorySeparatorChar.ToString()))
          {
            root += Path.DirectorySeparatorChar;
          }
          if (full.StartsWith(root, StringComparison.OrdinalIgnoreCase))
          {
            return true;
          }
        }
      }
      return false;
    }

    public static string TransformInRealPath(string localFile)
    {
      foreach (var pathProvider in PathProviders)
      {
        localFile = pathProvider.Value.Expand(localFile);
      }
      return localFile;
    }

    public static void SetInitialUrlIndex(List<string> list)
    {
      foreach (string url in list)
      {
        if (string.IsNullOrEmpty(url)) continue;

        if (!extensionUrlList.Contains(url))
          extensionUrlList.Add(url);
      }
    }

    public static List<string> GetInitialUrlIndex(List<string> list)
    {
      foreach (string url in extensionUrlList)
      {
        if (string.IsNullOrEmpty(url)) continue;

        if (!list.Contains(url))
          list.Add(url);
      }

      return list;
    }
  }
}