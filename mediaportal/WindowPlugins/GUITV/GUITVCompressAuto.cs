using System;
using MediaPortal.GUI.Library;
namespace WindowPlugins.GUITV
{
	/// <summary>
	/// Summary description for GUITVCompressAuto.
	/// </summary>
	public class GUITVCompressAuto : GUIWindow	
	{
		[SkinControlAttribute(5)]				protected GUISpinControl spinHour=null;
		[SkinControlAttribute(2)]				protected GUICheckMarkControl checkAutoCompress=null;
		[SkinControlAttribute(7)]			protected GUICheckMarkControl checkDeleteOriginal=null;

		public GUITVCompressAuto()
		{
			GetID=(int)GUIWindow.Window.WINDOW_TV_COMPRESS_AUTO;
		}
    
		public override bool Init()
		{
			return Load (GUIGraphicsContext.Skin+@"\mytvcompressauto.xml");
		}

		protected override void OnPageLoad()
		{
			base.OnPageLoad ();
			LoadSettings();
		}
		protected override void OnPageDestroy(int newWindowId)
		{
			base.OnPageDestroy (newWindowId);
			SaveSettings();
		}
		void LoadSettings()
		{
			using(MediaPortal.Profile.Xml   xmlreader=new MediaPortal.Profile.Xml("MediaPortal.xml"))
			{
				spinHour.Value = xmlreader.GetValueAsInt("autocompression","hour",4);
				checkDeleteOriginal.Selected= xmlreader.GetValueAsBool("autocompression","deleteoriginal",true);
				checkAutoCompress.Selected= xmlreader.GetValueAsBool("autocompression","enabled",true);
			}
			UpdateButtons();
		}

		void SaveSettings()
		{
			
			using(MediaPortal.Profile.Xml   xmlreader=new MediaPortal.Profile.Xml("MediaPortal.xml"))
			{
				xmlreader.SetValue("autocompression","hour",spinHour.Value);
				xmlreader.SetValueAsBool("autocompression","deleteoriginal",checkDeleteOriginal.Selected);
				xmlreader.SetValueAsBool("autocompression","enabled",checkAutoCompress.Selected);
			}
		}


		protected override void OnClicked(int controlId, GUIControl control, MediaPortal.GUI.Library.Action.ActionType actionType)
		{
			base.OnClicked (controlId, control, actionType);
			UpdateButtons();
		}
		void UpdateButtons()
		{
			spinHour.Disabled = !checkAutoCompress.Selected;
			checkDeleteOriginal.Disabled = !checkAutoCompress.Selected;
		}

	}
}
