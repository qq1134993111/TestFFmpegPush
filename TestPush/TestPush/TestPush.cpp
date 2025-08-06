#include <signal.h>

#include <iostream>
#include <set>
#include <map>
#include <memory>
#include <functional>

#include "PushCameraRtmp.h"
#include "PushCameraRtsp.h"
#include "CFFHandle.h"

std::string GetDeviceName(const std::string keyword = "USB 4K Camera Audio")
{
	avdevice_register_all();

	AVDeviceInfoList* device_list = nullptr;
	avdevice_list_input_sources(
		av_find_input_format("dshow"),
		nullptr, nullptr,
		&device_list
	);

	std::string str;
	for (int i = 0; i < device_list->nb_devices; i++)
	{
		printf("[%d] %s — %s\n",
			i,
			device_list->devices[i]->device_name,
			device_list->devices[i]->device_description);

		std::string desc = device_list->devices[i]->device_description;
		if (desc.find(keyword) != desc.npos)
		{
			str = desc;
			break;
		}
	}
	avdevice_free_list_devices(&device_list);
	return str;
}

//ffmpeg -list_devices true -f dshow -i dummy
//ffmpeg -f dshow -i video="HP HD Webcam":audio="Internal Microphone" -f gdigrab -framerate 30 -i desktop -filter_complex "overlay=10:10" -c:v libx264 -preset veryfast -c:a aac -f flv "rtmp://server.com/live/stream_key"
//ffmpeg -f dshow -i video="HP HD Webcam":audio="Internal Microphone" -c:v libx264 -preset veryfast -c:a aac -f rtsp "rtsp://server.com:554/live/stream_name"


int main(int argc, char** argv)
{
	PushCameraBase::Init(AV_LOG_WARNING);
	std::string input_uri = PushCameraBase::GetCameraAndmicrophoneInputString("USB 4K Camera", GetDeviceName("USB 4K Camera Audio"));
	std::string input_fmt_name = "dshow";

	if (0)
	{
		PushCameraRtsp rtsp;
		rtsp.InitInput(input_uri, input_fmt_name);
		//rtsp.InitInput(/*input_uri*/"C:\\Users\\HAO\\Downloads\\18.mp4", /*input_fmt_name*/"mp4");
		//rtsp.InitOutput("rtsp://43.153.201.69:554/live/camera", "rtsp");
		rtsp.InitOutput("rtsp://127.0.0.1:554/live/camera", "rtsp");
		rtsp.HandlePacket();
	}

	if (0)
	{
		PushCameraRtmp rtmp;
		rtmp.InitInput(/*input_uri*/"C:\\Users\\HAO\\Downloads\\18.mp4", /*input_fmt_name*/"mp4");
		//rtmp.InitOutput("rtmp://43.153.201.69:1935/live/camera", "flv");
		rtmp.InitOutput("rtmp://127.0.0.1:1935/live/camera", "flv");
		rtmp.HandlePacket();
	}

	if (1)
	{

		//input_uri = PushCameraBase::GetCameraAndmicrophoneInputString("USB 4K Camera", "");
		//input_uri= PushCameraBase::GetCameraAndmicrophoneInputString("", GetDeviceName("USB 4K Camera Audio"));
		CFFHandle handle;
		handle.InitInput(input_uri, input_fmt_name);
		//handle.InitOutput("rtmp://127.0.0.1:1935/live/camera", "flv");
		handle.InitOutput("rtsp://127.0.0.1:554/live/camera", "rtsp");
		handle.HandlePakege();
	}


}