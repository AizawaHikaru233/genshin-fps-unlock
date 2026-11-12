using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace unlockfps_nc.Model
{
    public class Config
    {
        public string GamePath { get; set; } = "";

        public bool AutoStart { get; set; }
        public bool AutoClose { get; set; }
        public bool PopupWindow { get; set; }
        public bool Fullscreen { get; set; } = true;
        public bool UseCustomRes { get; set; }
        public bool IsExclusiveFullscreen { get; set; }
        public bool StartMinimized { get; set; }
        public bool UsePowerSave { get; set; }
        public bool SuspendLoad { get; set; }
        public bool UseMobileUI { get; set; }
        public bool UseHDR { get; set; }

        public int FPSTarget { get; set; } = 120;

        // Makes the game read a stock frame rate tier while the real one stays unlocked,
        // so an anti-cheat watching the frame rate does not see the unlocked value. Read by
        // UnlockerStub.dll from this same file; defaults to on.
        public bool FpsClamp { get; set; } = true;

        public int CustomResX { get; set; } = 1920;
        public int CustomResY { get; set; } = 1080;
        public int MonitorNum { get; set; } = 1;
        public int Priority { get; set; } = 3;
        public string AdditionalCommandLine { get; set; } = "";

        public long LastVersionNotify { get; set; } = 0;

        public List<string> DllList { get; set; } = new();
    }
}
