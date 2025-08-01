#pragma once
#include <string>
#include <atomic>
#include "PushCameraBase.h"


class PushCameraRtmp:public PushCameraBase
{
public:
	//rtmp 中 out_uri 为 rtmp://127.0.0.1:1935/live/camera 这样的  out_fmt_name为flv
	virtual bool InitOutput(const std::string& out_uri, const std::string& out_format_name, av::Dictionary out_format_options = {}) override;
	virtual bool HandlePacket() override;
	bool IsRunning()
	{
		return is_running_;
	}
	void Stop()
	{
		is_running_ = false;
	}
private:
	int16_t fps_ = 30;
	std::atomic<bool> is_running_=false;
};
