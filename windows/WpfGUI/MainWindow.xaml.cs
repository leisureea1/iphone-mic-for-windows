using System;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using System.Windows.Controls;

namespace iPhoneMic
{
    public partial class MainWindow : Window
    {
        private DispatcherTimer _timer;

        public MainWindow()
        {
            InitializeComponent();

            // Initialize C++ Backend
            BackendInterop.Backend_Init();

            // Refresh UI state
            RefreshDevices();
            MonitorCheckBox.IsChecked = BackendInterop.Backend_GetMonitorAudio();

            // Setup update timer (30fps)
            _timer = new DispatcherTimer();
            _timer.Interval = TimeSpan.FromMilliseconds(33);
            _timer.Tick += Timer_Tick;
            _timer.Start();
        }

        private void Timer_Tick(object? sender, EventArgs e)
        {
            // Update Connection Status
            bool connected = BackendInterop.Backend_GetConnectionStatus();
            if (connected)
            {
                StatusText.Text = "已连接";
                StatusBorder.Background = new SolidColorBrush(Color.FromRgb(40, 167, 69)); // Green
            }
            else
            {
                StatusText.Text = "未连接";
                StatusBorder.Background = new SolidColorBrush(Color.FromRgb(220, 53, 69)); // Red
            }

            // Update Audio Levels
            BackendInterop.Backend_GetAudioLevels(out float left, out float right);
            
            // Map linear to logarithmic visually or just use linear
            LevelMeterL.Value = Math.Min(1.0, left * 2.0);
            LevelMeterR.Value = Math.Min(1.0, right * 2.0);
        }

        private void RefreshDevices()
        {
            DeviceComboBox.SelectionChanged -= DeviceComboBox_SelectionChanged;
            
            string[] devices = BackendInterop.GetOutputDevices();
            DeviceComboBox.Items.Clear();
            foreach (var dev in devices)
            {
                DeviceComboBox.Items.Add(dev);
            }

            int selectedIndex = BackendInterop.Backend_GetSelectedDeviceIndex();
            if (selectedIndex >= 0 && selectedIndex < DeviceComboBox.Items.Count)
            {
                DeviceComboBox.SelectedIndex = selectedIndex;
            }

            DeviceComboBox.SelectionChanged += DeviceComboBox_SelectionChanged;
        }

        private void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            RefreshDevices();
        }

        private void DeviceComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (DeviceComboBox.SelectedIndex >= 0)
            {
                BackendInterop.Backend_SetOutputDevice(DeviceComboBox.SelectedIndex);
            }
        }

        private void MonitorCheckBox_Checked(object sender, RoutedEventArgs e)
        {
            BackendInterop.Backend_SetMonitorAudio(true);
        }

        private void MonitorCheckBox_Unchecked(object sender, RoutedEventArgs e)
        {
            BackendInterop.Backend_SetMonitorAudio(false);
        }

        protected override void OnClosed(EventArgs e)
        {
            _timer.Stop();
            BackendInterop.Backend_Shutdown();
            base.OnClosed(e);
            
            // Force exit to ensure background threads die
            Application.Current.Shutdown();
            Environment.Exit(0);
        }
    }
}