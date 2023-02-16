#include "ultra64.h"
#include "multilibultra.hpp"
#include "SDL.h"
#include "SDL_audio.h"
#include <cassert>

static SDL_AudioDeviceID audio_device = 0;
static uint32_t sample_rate = 48000;

void Multilibultra::init_audio() {
	// Initialize SDL audio.
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	// Pick an initial dummy sample rate; this will be set by the game later to the true sample rate.
	set_audio_frequency(48000);
}

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
		.callback = nullptr,//feed_audio, // Use a callback as QueueAudio causes popping
		.userdata = nullptr
	};

	audio_device = SDL_OpenAudioDevice(nullptr, false, &spec_desired, nullptr, 0);
	if (audio_device == 0) {
		printf("SDL Error: %s\n", SDL_GetError());
		fflush(stdout);
		assert(false);
	}
	SDL_PauseAudioDevice(audio_device, 0);
	sample_rate = freq;
}

void Multilibultra::queue_audio_buffer(RDRAM_ARG PTR(s16) audio_data_, uint32_t byte_count) {
	// Buffer for holding the output of swapping the audio channels. This is reused across
	// calls to reduce runtime allocations.
	static std::vector<uint16_t> swap_buffer;

	// Ensure that the byte count is an integer multiple of samples.
	assert((byte_count & 1) == 0);

	// Calculate the number of samples from the number of bytes.
	uint32_t sample_count = byte_count / sizeof(s16);

	// Make sure the swap buffer is large enough to hold all the incoming audio data.
	if (sample_count > swap_buffer.size()) {
		swap_buffer.resize(sample_count);
	}

	// Swap the audio channels into the swap buffer to correct for the address xor caused by endianness handling.
	s16* audio_data = TO_PTR(s16, audio_data_);
	for (size_t i = 0; i < sample_count; i += 2) {
		swap_buffer[i + 0] = audio_data[i + 1];
		swap_buffer[i + 1] = audio_data[i + 0];
	}

	// Queue the swapped audio data.
	SDL_QueueAudio(audio_device, swap_buffer.data(), byte_count);
}

// If there's ever any audio popping, check here first. Some games are very sensitive to
// the remaining sample count and reporting a number that's too high here can lead to issues.
// Reporting a number that's too low can lead to audio lag in some games.
uint32_t Multilibultra::get_remaining_audio_bytes() {
	// Get the number of remaining buffered audio bytes.
	uint32_t buffered_byte_count = SDL_GetQueuedAudioSize(audio_device);
	
	// Adjust the reported count to be four refreshes in the future, which helps ensure that
	// there are enough samples even if the game experiences a small amount of lag. This prevents
	// audio popping on games that use the buffered audio byte count to determine how many samples
	// to generate.
	uint32_t samples_per_vi = (sample_rate / 60);
	if (buffered_byte_count > (4u * samples_per_vi)) {
		buffered_byte_count -= (4u * samples_per_vi);
	} else {
		buffered_byte_count = 0;
	}
	return buffered_byte_count;
}
