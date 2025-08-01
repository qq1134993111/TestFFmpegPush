#pragma once
//SDL 3.2
#include <SDL3/SDL.h>

class SDLPlayAudio
{
public:
	static bool Init(SDL_InitFlags flags = SDL_INIT_EVENTS)
	{
		//³õÊ¼»¯ SDL
		if (!SDL_Init(flags))
		{
			SDL_Log("SDL_Init failed: %s", SDL_GetError());
			return false;
		}

		return true;
	}

	static void Quit()
	{
		SDL_Quit();
	}

	~SDLPlayAudio()
	{
		ClearStream();
	}

	bool OpenDeviceStream(int channels, int freq = 44100, SDL_AudioFormat format = SDL_AUDIO_S16)
	{
		SDL_AudioSpec spec;
		spec.freq = freq;
		spec.channels = channels;
		spec.format = format;

		SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
		if (!stream)
		{
			SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
			return false;
		}

		/* SDL_OpenAudioDeviceStream starts the device paused. You have to tell it to start! */
		SDL_ResumeAudioStreamDevice(stream);

		stream_ = stream;
		return true;
	}

	void ClearStream()
	{
		if (stream_ != nullptr)
		{
			SDL_ClearAudioStream(stream_);
			stream_ = nullptr;
		}
	}

	int GetStreamQueued()
	{
		return SDL_GetAudioStreamQueued(stream_);
	}

	bool PutStreamData(const char* data, int size, int wait_queued_size = 0)
	{
		if (wait_queued_size > 0)
		{
			int remaininglen = SDL_GetAudioStreamQueued(stream_);
			while (remaininglen > wait_queued_size)
			{
				SDL_Delay(1);
				/* feed more data to the stream. It will queue at the end, and trickle out as the hardware needs more data. */
				remaininglen = SDL_GetAudioStreamQueued(stream_);
			}
		}

		return  SDL_PutAudioStreamData(stream_, data, size);
	}
private:
	SDL_AudioStream* stream_ = nullptr;
};

