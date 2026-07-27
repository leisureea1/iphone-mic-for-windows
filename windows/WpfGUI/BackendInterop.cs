using System;
using System.Runtime.InteropServices;

namespace iPhoneMic
{
    public static class BackendInterop
    {
        private const string DllName = "iphone_mic_backend.dll";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void Backend_Init();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void Backend_Shutdown();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr Backend_GetOutputDevicesCSV();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int Backend_GetSelectedDeviceIndex();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void Backend_SetOutputDevice(int index);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void Backend_SetMonitorAudio(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool Backend_GetMonitorAudio();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool Backend_GetConnectionStatus();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void Backend_GetAudioLevels(out float left, out float right);
        
        public static string[] GetOutputDevices()
        {
            IntPtr ptr = BackendInterop.Backend_GetOutputDevicesCSV();
            if (ptr != IntPtr.Zero)
            {
                string csv = Marshal.PtrToStringAnsi(ptr);
                if (!string.IsNullOrEmpty(csv))
                {
                    return csv.Split('|');
                }
            }
            return new string[0];
        }
    }
}
