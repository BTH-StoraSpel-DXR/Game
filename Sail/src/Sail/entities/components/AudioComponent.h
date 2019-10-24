#pragma once

#include "Sail/entities/components/Component.h"
#include <string>
#include <stack>

class AudioComponent : public Component<AudioComponent>
{
public:
	AudioComponent();
	virtual ~AudioComponent();

	//Audio::SoundInfo_General m_sounds[Audio::SoundType::COUNT]{};
	//std::vector<Audio::SoundInfo_Unique> m_soundsUnique[Audio::SoundType::COUNT]{};

	// An 'easy-mode' helper function for starting/stopping a streamed sound
	void streamSoundRequest_HELPERFUNC(std::string filename, bool startTrue_stopFalse, float volume, bool isPositionalAudio, bool isLooping);
	// A helpful function that simplifies the process of defining a new sound
	void defineSoundGeneral(Audio::SoundType type, Audio::SoundInfo_General info);
	void defineSoundUnique(Audio::SoundType type, Audio::SoundInfo_Unique info);

	// VARIABLE DEFINITIONS/CLARIFICATIONS
		// � string = filename
		// � bool = TRUE if START-request, FALSE if STOP-request
	std::list<std::pair<std::string, Audio::StreamRequestInfo>> m_streamingRequests;
	// VARIABLE DEFINITIONS/CLARIFICATIONS
		// � string = filename
		// � int = ID of playing streaming; needed for STOPPING the streamed sound
	std::list<std::pair<std::string, std::pair<int, bool>>> m_currentlyStreaming;
};
