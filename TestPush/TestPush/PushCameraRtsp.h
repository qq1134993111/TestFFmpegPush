#pragma once
#include <string>

#include "PushCameraBase.h"


class PushCameraRtsp :public PushCameraBase
{
public:
	//rtsp �� out_uri Ϊ rtsp://127.0.0.1:554/live/camera ������  out_fmt_nameΪrtsp
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
protected:
	bool FlushEnd();
private:
	int16_t fps_ = 30;
	std::atomic<bool> is_running_ = false;
};
