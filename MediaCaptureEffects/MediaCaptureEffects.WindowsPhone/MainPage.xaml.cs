using Nokia.Graphics.Imaging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Windows.Devices.Enumeration;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.Graphics.Display;
using Windows.Media;
using Windows.Media.Capture;
using Windows.Media.MediaProperties;
using Windows.Storage;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=234238

namespace MediaCaptureEffects
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        App app;
        DeviceInformationCollection _devices;
        IReadOnlyList<IMediaEncodingProperties> _encodingPreviewProperties;
        IReadOnlyList<IMediaEncodingProperties> _encodingRecorderProperties;
        MediaExtensionManager _extensionManager = new MediaExtensionManager();
        StorageFile _tempStorageFile;

        public MainPage()
        {
            this.InitializeComponent();

            this.NavigationCacheMode = NavigationCacheMode.Required;
        }

        /// <summary>
        /// Invoked when this page is about to be displayed in a Frame.
        /// </summary>
        /// <param name="e">Event data that describes how this page was reached.
        /// This parameter is typically used to configure the page.</param>
        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);

            _tempStorageFile = await ApplicationData.Current.TemporaryFolder.CreateFileAsync("test.mp4", CreationCollisionOption.ReplaceExisting);

            DisplayInformation.AutoRotationPreferences = DisplayOrientations.Landscape;

            //put references in app so that these can be properly shutdown when app is suspsended.
            app = (App)Application.Current;
            app.PreviewElement = capturePreview;

            _devices = await DeviceInformation.FindAllAsync(DeviceClass.VideoCapture);
            ListDeviceDetails();

            await StartMediaCapture();

            var core = Windows.UI.Core.CoreWindow.GetForCurrentThread();
            core.VisibilityChanged += core_VisibilityChanged;
        }

        async void core_VisibilityChanged(Windows.UI.Core.CoreWindow sender, Windows.UI.Core.VisibilityChangedEventArgs args)
        {
            if (args.Visible)
            {
                await StartMediaCapture();
            }
            else
            {
                await StopMediaCapture();
            }
        }

        async Task StartMediaCapture()
        {
            Debug.WriteLine("Starting MediaCapture");
            app.MediaCapture = new MediaCapture();
            var selectedDevice = _devices.FirstOrDefault(x => x.EnclosureLocation != null && x.EnclosureLocation.Panel == Windows.Devices.Enumeration.Panel.Back);
            if (selectedDevice == null)
                selectedDevice = _devices.First();
            await app.MediaCapture.InitializeAsync(new MediaCaptureInitializationSettings
            {
                VideoDeviceId = selectedDevice.Id
            });
            app.PreviewElement.Source = app.MediaCapture;

            _encodingPreviewProperties = app.MediaCapture.VideoDeviceController.GetAvailableMediaStreamProperties(MediaStreamType.VideoPreview);
            _encodingRecorderProperties = app.MediaCapture.VideoDeviceController.GetAvailableMediaStreamProperties(MediaStreamType.VideoRecord);
            ListAllResolutionDetails();

            var selectedPreviewProperties = _encodingPreviewProperties.First(x => ((VideoEncodingProperties)x).Width == 800);
            ListResolutionDetails((VideoEncodingProperties)selectedPreviewProperties);
            await app.MediaCapture.VideoDeviceController.SetMediaStreamPropertiesAsync(MediaStreamType.VideoPreview, selectedPreviewProperties);

            var selectedRecordingProperties = _encodingRecorderProperties.First(x => ((VideoEncodingProperties)x).Width == _encodingRecorderProperties.Max(y => ((VideoEncodingProperties)y).Width));
            ListResolutionDetails((VideoEncodingProperties)selectedRecordingProperties);
            await app.MediaCapture.VideoDeviceController.SetMediaStreamPropertiesAsync(MediaStreamType.VideoRecord, selectedRecordingProperties);

            PropertySet testSet = new PropertySet();
            //int test = (int)LomoVignetting.High;
            testSet.Add("filterList", new List<string>() { "LomoFilter" });
            //testSet.Add("SolarizeFilter", new List<string>() { "" });
            testSet.Add("LomoFilter", String.Format("0.5,0.8,{0},{1}", (int)LomoVignetting.High, (int)LomoStyle.Blue));

            await app.MediaCapture.AddEffectAsync(MediaStreamType.VideoPreview, "ImagingEffects.ImagingEffect", testSet);
            //app.MediaCapture.SetPreviewRotation(VideoRotation.Clockwise90Degrees);  //need this if portrait mode or landscapeflipped.
            app.IsPreviewing = true;
            await app.MediaCapture.StartPreviewAsync();

            if (app.MediaCapture.VideoDeviceController.FocusControl.Supported) //.SetPresetAsync(Windows.Media.Devices.FocusPreset.Manual);
            {
                await app.MediaCapture.VideoDeviceController.FocusControl.SetPresetAsync(Windows.Media.Devices.FocusPreset.Manual);
            }
            else
            {
                app.MediaCapture.VideoDeviceController.Focus.TrySetAuto(false);
            }
        }

        async Task StopMediaCapture()
        {
            Debug.WriteLine("Disposing MediaCapture");
            app.IsPreviewing = false;
            await app.MediaCapture.StopPreviewAsync();
            app.MediaCapture.Dispose();
        }



        private void ListDeviceDetails()
        {
            int i = 0;

            foreach (var device in _devices)
            {
                Debug.WriteLine("* Device [{0}]", i++);
                if (device.EnclosureLocation != null)
                {
                    Debug.WriteLine("EnclosureLocation.InDock: " + device.EnclosureLocation.InDock);
                    Debug.WriteLine("EnclosureLocation.InLid: " + device.EnclosureLocation.InLid);
                    Debug.WriteLine("EnclosureLocation.Panel: " + device.EnclosureLocation.Panel);
                }
                else
                {
                    Debug.WriteLine("Not in Enclosure");
                }
                Debug.WriteLine("Id: " + device.Id);
                Debug.WriteLine("IsDefault: " + device.IsDefault);
                Debug.WriteLine("IsEnabled: " + device.IsEnabled);
                Debug.WriteLine("Name: " + device.Name);
                Debug.WriteLine("IsDefault: " + device.IsDefault);

                foreach (var property in device.Properties)
                {
                    Debug.WriteLine(property.Key + ": " + property.Value);
                }
            }
        }

        private void ListAllResolutionDetails()
        {
            int i = 0;
            foreach (VideoEncodingProperties prop in _encodingPreviewProperties)
            {
                ListResolutionDetails(prop, i);
            }
        }

        private void ListResolutionDetails(VideoEncodingProperties prop, int i = 0)
        {
            Debug.WriteLine("* Property [{0}]", i++);
            Debug.WriteLine("Bitrate: " + prop.Bitrate);
            Debug.WriteLine("Framerate: " + prop.FrameRate);
            Debug.WriteLine("Resolution: " + prop.Width + ", " + prop.Height);
            Debug.WriteLine("PixelAspectRatio: " + prop.PixelAspectRatio);
            Debug.WriteLine("ProfileId: " + prop.ProfileId);
            Debug.WriteLine("Subtype: " + prop.Subtype);
            Debug.WriteLine("Type: " + prop.Type);

            foreach (var property in prop.Properties)
            {
                Debug.WriteLine(property.Key + ": " + property.Value);
            }
        }

        private async void AppBarButton_Click(object sender, RoutedEventArgs e)
        {
            if (app.IsRecording == false)
            {
                await app.MediaCapture.ClearEffectsAsync(MediaStreamType.VideoPreview);
                await app.MediaCapture.AddEffectAsync(MediaStreamType.VideoRecord, "ImagingEffects.ImagingEffect", null);
                await app.MediaCapture.StartRecordToStorageFileAsync(MediaEncodingProfile.CreateMp4(VideoEncodingQuality.HD720p), _tempStorageFile);
                app.IsRecording = true;
            }
            else
            {
                await app.MediaCapture.StopRecordAsync();
                await app.MediaCapture.ClearEffectsAsync(MediaStreamType.VideoRecord);
                await app.MediaCapture.AddEffectAsync(MediaStreamType.VideoPreview, "ImagingEffects.ImagingEffect", null);
                app.IsRecording = false;
                Windows.Storage.Pickers.FileSavePicker picker = new Windows.Storage.Pickers.FileSavePicker();
                picker.SuggestedFileName = "test.mp4";
                picker.FileTypeChoices.Add("MP4 movie", new List<string>() { ".mp4" });
                picker.SuggestedStartLocation = Windows.Storage.Pickers.PickerLocationId.VideosLibrary;
                //var file = await picker.PickSaveFileAsync();
                //if (file != null)
                    //await _tempStorageFile.CopyAndReplaceAsync(file);
            }
        }
    }
}
