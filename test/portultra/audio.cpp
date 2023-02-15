#include "ultra64.h"
#include "multilibultra.hpp"
#include "SDL.h"
#include "SDL_audio.h"
#include <cassert>

static SDL_AudioDeviceID audio_device = 0;

void Multilibultra::init_audio() {
	// Initialize SDL audio.
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	// Pick an initial dummy sample rate; this will be set by the game later to the true sample rate.
	set_audio_frequency(48000);
}

void SDLCALL feed_audio(void* userdata, Uint8* stream, int len);

void Multilibultra::set_audio_frequency(uint32_t freq) {
	if (audio_device != 0) {
		SDL_CloseAudioDevice(audio_device);
	}
	SDL_AudioSpec spec_desired{
		.freq = (int)freq,
		.format = AUDIO_S16,
		.channels = 2,
		.silence = 0, // calculated
		.samples = 0x100, // Fairly small sample count to reduce the latency of internal buffering
		.padding = 0, // unused
		.size = 0, // calculated
		.callback = feed_audio, // Use a callback as QueueAudio causes popping
		.userdata = nullptr
	};

	audio_device = SDL_OpenAudioDevice(nullptr, false, &spec_desired, nullptr, 0);
	if (audio_device == 0) {
		printf("SDL Error: %s\n", SDL_GetError());
		fflush(stdout);
		assert(false);
	}
	SDL_PauseAudioDevice(audio_device, 0);
}

// Struct representing a queued audio buffer.
struct AudioBuffer {
	// All samples in the buffer, including those that have already been sent.
	std::vector<int16_t> samples;
	// The count of samples that have already been sent to the audio device.
	size_t used_samples = 0;

	// Helper methods.
	size_t remaining_samples() const { return samples.size() - used_samples; };
	size_t remaining_bytes() const { return remaining_samples() * sizeof(samples[0]); };
	int16_t* first_unused_sample() { return &samples[used_samples]; }
	bool empty() { return used_samples == samples.size(); }
};

// Mutex for locking the queued audio buffer list.
std::mutex audio_buffers_mutex;
// The queued audio buffer list, holds a list of buffers that have been queued by the game.
std::vector<AudioBuffer> audio_buffers;

void SDLCALL feed_audio(void* userdata, Uint8* stream, int byte_count) {
	// Ensure that the byte count is an integer multiple of samples.
	assert((byte_count & 1) == 0);

	// Calculate the sample count from the byte count.
	size_t remaining_samples = byte_count / sizeof(int16_t);

	// Lock the queued audio buffer list.
	std::lock_guard lock{ audio_buffers_mutex };

	// Empty the audio buffers until we've sent all the required samples
	// or until there are no samples left in the audio buffers.
	while (!audio_buffers.empty() && remaining_samples > 0) {
		auto& cur_buffer = audio_buffers.front();
		// Prevent overrunning either the input or output buffer.
		size_t to_copy = std::min(remaining_samples, cur_buffer.remaining_samples());
		// Copy samples from the input buffer to the output one.
		memcpy(stream, cur_buffer.first_unused_sample(), to_copy * sizeof(int16_t));
		// Advance the output buffer by the copied byte count.
		stream += to_copy * sizeof(int16_t);
		// Advance the input buffer by the copied sample count.
		cur_buffer.used_samples += to_copy;
		// Updated the remaining sample count.
		remaining_samples -= to_copy;

		// If the input buffer was emptied, remove it from the list of queued buffers.
		if (cur_buffer.empty()) {
			audio_buffers.erase(audio_buffers.begin());
		}
	}

	// Zero out any remaining audio data to lessen audio issues during lag
	memset(stream, 0, remaining_samples * sizeof(int16_t));
}

void Multilibultra::queue_audio_buffer(RDRAM_ARG PTR(s16) audio_data_, uint32_t byte_count) {
	// Ensure that the byte count is an integer multiple of samples.
	assert((byte_count & 1) == 0);

	s16* audio_data = TO_PTR(s16, audio_data_);
	// Calculate the number of samples from the number of bytes.
	uint32_t sample_count = byte_count / sizeof(s16);

	// Lock the queued audio buffer list.
	std::lock_guard lock{ audio_buffers_mutex };

	// Set up a new queued audio buffer.
	AudioBuffer& new_buf = audio_buffers.emplace_back();
	new_buf.samples.resize(sample_count);
	new_buf.used_samples = 0;

	// Copy the data into the new buffer.
	// Swap the audio channels to correct for the address xor caused by endianness handling.
	for (size_t i = 0; i < sample_count; i += 2) {
		new_buf.samples[i + 0] = audio_data[i + 1];
		new_buf.samples[i + 1] = audio_data[i + 0];
	}
}

// If there's ever any audio popping, check here first. Some games are very sensitive to
// the remaining sample count and reporting a number that's too high here can lead to issues.
// Reporting a number that's too low can lead to audio lag in some games.
uint32_t Multilibultra::get_remaining_audio_bytes() {
	// Calculate the number of samples still in the queued audio buffers
	size_t buffered_byte_count = 0;
	{
		// Lock the queued audio buffer list.
		std::lock_guard lock{ audio_buffers_mutex };

		// Gather the remaining byte count of the next buffer, if any exists.
		if (!audio_buffers.empty()) {
			buffered_byte_count = audio_buffers.front().remaining_bytes();
		}
	}
	// Add the number of remaining bytes in the audio data that's been sent to the device.
	buffered_byte_count += SDL_GetQueuedAudioSize(audio_device);
	
	// Add a slight positive scaling bias, which helps audio respond quicker. Remove the bias
	// if games have popping issues.
	return buffered_byte_count + (buffered_byte_count / 10);
}
