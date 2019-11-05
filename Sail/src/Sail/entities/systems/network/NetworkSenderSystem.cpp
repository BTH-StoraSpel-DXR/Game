#include "pch.h"
#include "NetworkSenderSystem.h"

#include "receivers/NetworkReceiverSystem.h"

#include "Sail/entities/components/NetworkSenderComponent.h"
#include "Sail/entities/components/OnlineOwnerComponent.h"
#include "Sail/entities/components/LocalOwnerComponent.h"
#include "Sail/entities/Entity.h"

#include "Network/NWrapperSingleton.h"
#include "Sail/../../libraries/cereal/archives/portable_binary.hpp"

#include "../src/Network/NWrapperSingleton.h"
#include "Sail/utils/GameDataTracker.h"

#include <string>
#include <vector>

//#define _LOG_TO_FILE
#if defined(DEVELOPMENT) && defined(_LOG_TO_FILE)
#include <fstream>
static std::ofstream out("LogFiles/NetworkSenderSystem.cpp.log");
#endif

NetworkSenderSystem::NetworkSenderSystem() : BaseComponentSystem() {
	registerComponent<NetworkSenderComponent>(true, true, true);
	registerComponent<TransformComponent>(false, true, false);
}

NetworkSenderSystem::~NetworkSenderSystem() {
	while (!m_eventQueue.empty()) {
		NetworkSenderEvent* pEvent = m_eventQueue.front();
		m_eventQueue.pop();
		delete pEvent;
	}
}

void NetworkSenderSystem::init(Netcode::PlayerID playerID, NetworkReceiverSystem* receiverSystem) {
	m_playerID = playerID;
	m_receiverSystem = receiverSystem;
}

/*
  The construction of messages needs to match how the NetworkReceiverSystem parses them so
  any changes made here needs to be made there as well!


	Logical structure of the package that will be sent by this function:
	--------------------------------------------------
	| PlayerID        senderID                       |
	| size_t          nrOfEntities                   |
	|     ComponentID     entity[0].id               |
	|     EntityType      entity[0].type             |
	|     size_t          nrOfMessages               |
	|         MessageType     entity[0].messageType  |
	|         MessageData     entity[0].data         |
	|         ...                                    |
	|     ComponentID     entity[1].id               |
	|     EntityType      entity[1].type             |
	|     size_t          nrOfMessages               |
	|         MessageType     entity[0].messageType	 |
	|         MessageData     entity[0].data         |
	|         ...                                    |
	|     ComponentID     entity[2].id               |
	|     EntityType      entity[2].type             |
	|     size_t          nrOfMessages               |
	|         MessageType     entity[0].messageType  |
	|         MessageData     entity[0].data         |
	|         ...                                    |
	|     ...                                        |
	| size_t          nrOfEvents                     |
	|     MessageType     eventType[0]               |
	|     EventData       eventData[0]               |
	|     ...                                        |
	| ...                                            |
	--------------------------------------------------

*/
void NetworkSenderSystem::update() {
	// Binary data that will be sent over the network
	std::ostringstream osToOthers(std::ios::binary);
	Netcode::OutArchive sendToOthers(osToOthers);

	// Binary data that will be sent to our own receiver system so that the network event handling
	// code doesn't need to be duplicated.
	std::ostringstream osToSelf(std::ios::binary);
	Netcode::OutArchive sendToSelf(osToSelf);

	// -+-+-+-+-+-+-+-+ Per-frame sends to per-frame receives via components -+-+-+-+-+-+-+-+ 
	// Send our playerID so that we can ignore this packet when it gets back to us from the host
	sendToOthers(m_playerID);
	sendToSelf(Netcode::MESSAGE_FROM_SELF_ID);

	// Write nrOfEntities
	sendToOthers(entities.size());
	sendToSelf(size_t{0}); // SenderComponent messages should not be sent to ourself

	for (auto e : entities) {
		NetworkSenderComponent* nsc = e->getComponent<NetworkSenderComponent>();
		sendToOthers(nsc->m_id);                // ComponentID    
		sendToOthers(nsc->m_entityType);        // Entity type
		sendToOthers(nsc->m_dataTypes.size());  // NrOfMessages

		// Per type of data
		for (auto& messageType : nsc->m_dataTypes) {
			sendToOthers(messageType);          // Current MessageType
#if defined(DEVELOPMENT) && defined(_LOG_TO_FILE)
			out << "SenderComp: " << Netcode::MessageNames[(int)(messageType) - 1] << "\n";
#endif
			writeMessageToArchive(messageType, e, sendToOthers); // Add to archive depending on the message
		}
	}

	// -+-+-+-+-+-+-+-+ Per-instance events via eventQueue -+-+-+-+-+-+-+-+ 
	sendToOthers(m_eventQueue.size());
	sendToSelf(m_nrOfEventsToSendToSelf.load());

	while (!m_eventQueue.empty()) {
		NetworkSenderEvent* pE = m_eventQueue.front();
#if defined(DEVELOPMENT) && defined(_LOG_TO_FILE)
		out << "Event: " << Netcode::MessageNames[(int)(pE->type)-1] << "\n";
#endif
		writeEventToArchive(pE, sendToOthers);
		if (pE->alsoSendToSelf) {
			writeEventToArchive(pE, sendToSelf);
		}

		m_eventQueue.pop();
		delete pE;
	}
	m_nrOfEventsToSendToSelf = 0;


	// -+-+-+-+-+-+-+-+ send the serialized archive over the network -+-+-+-+-+-+-+-+ 
	std::string binaryDataToSendToOthers = osToOthers.str();
	if (NWrapperSingleton::getInstance().isHost()) {
		NWrapperSingleton::getInstance().getNetworkWrapper()->sendSerializedDataAllClients(binaryDataToSendToOthers);
	} else {
		NWrapperSingleton::getInstance().getNetworkWrapper()->sendSerializedDataToHost(binaryDataToSendToOthers);
	}


	// -+-+-+-+-+-+-+-+ send Events directly to our own ReceiverSystem -+-+-+-+-+-+-+-+ 
	std::string binaryDataToSendToSelf = osToSelf.str();
	m_receiverSystem->pushDataToBuffer(binaryDataToSendToSelf);



	// -+-+-+-+-+-+-+-+ Host forwards all messages to all clients -+-+-+-+-+-+-+-+ 
	std::scoped_lock lock(m_forwardBufferLock);
	// The host forwards all the incoming messages they have to all the clients
	while (!m_HOSTONLY_dataToForward.empty()) {
		std::string dataFromClient = m_HOSTONLY_dataToForward.front();


		// This if statement shouldn't be needed since m_dataToForwardToClients will be empty unless you're the host
		if (NWrapperSingleton::getInstance().isHost()) {
			NWrapperSingleton::getInstance().getNetworkWrapper()->sendSerializedDataAllClients(dataFromClient);
		}
		m_HOSTONLY_dataToForward.pop();
	}
}

void NetworkSenderSystem::queueEvent(NetworkSenderEvent* type) {
	std::lock_guard<std::mutex> lock(m_queueMutex);

	m_eventQueue.push(type);

#ifdef DEVELOPMENT
	// Don't send broken events to others or to yourself
	if (type->type < Netcode::MessageType::CREATE_NETWORKED_ENTITY || type->type >= Netcode::MessageType::EMPTY) {
		Logger::Error("Attempted to send invalid message\n");
		return;
	}
#endif

	// if the event will be sent to ourself then increment the size counter
	if (type->alsoSendToSelf) {
		m_nrOfEventsToSendToSelf++;
	}
}


// ONLY DO FOR THE HOST
// Push incoming data strings to the back of a FIFO list which will be forwarded to all other players
void NetworkSenderSystem::pushDataToBuffer(std::string data) {
	std::scoped_lock lock(m_forwardBufferLock);
	m_HOSTONLY_dataToForward.push(data);
}

const std::vector<Entity*>& NetworkSenderSystem::getEntities() const {
	return entities;
}

// TODO: Test this to see if it's actually needed or not
void NetworkSenderSystem::stop() {
	// Loop through networked entities and serialize their data.
	std::ostringstream osToOthers(std::ios::binary);
	Netcode::OutArchive sendToOthers(osToOthers);

	sendToOthers(m_playerID);
	sendToOthers(size_t{0}); // Write nrOfEntities

	// -+-+-+-+-+-+-+-+ Per-instance events via eventQueue -+-+-+-+-+-+-+-+ 
	bool ended = false;

	while (!m_eventQueue.empty()) {
		NetworkSenderEvent* pE = m_eventQueue.front();		// Fetch
		if ((pE->type == Netcode::MessageType::MATCH_ENDED || pE->type == Netcode::MessageType::SEND_ALL_BACK_TO_LOBBY) && ended == false) {
			ended = true;
			sendToOthers(size_t{1}); // Write nrOfEvents
			writeEventToArchive(pE, sendToOthers);
		}
		m_eventQueue.pop();
		delete pE;
	}


	if (ended) {
		// send the serialized archive over the network
		std::string binaryData = osToOthers.str();
		if (NWrapperSingleton::getInstance().isHost()) {
			NWrapperSingleton::getInstance().getNetworkWrapper()->sendSerializedDataAllClients(binaryData);
		} else {
			NWrapperSingleton::getInstance().getNetworkWrapper()->sendSerializedDataToHost(binaryData);
		}
	}
}

void NetworkSenderSystem::addEntityToListONLYFORNETWORKRECIEVER(Entity* e) {
	entities.push_back(e);
}

void NetworkSenderSystem::writeMessageToArchive(Netcode::MessageType& messageType, Entity* e, Netcode::OutArchive& ar) {
	// Package it depending on the type
	switch (messageType) {
		// Send necessary info to create the networked entity 
	case Netcode::MessageType::CREATE_NETWORKED_ENTITY:
	{
		TransformComponent* t = e->getComponent<TransformComponent>();
		ArchiveHelpers::saveVec3(ar, t->getTranslation()); // Send translation

		// When the remote entity has been created we want to update translation and rotation of that entity
		auto networkComp = e->getComponent<NetworkSenderComponent>();

		// Stop sending Create messages after the first one
		networkComp->removeMessageType(Netcode::MessageType::CREATE_NETWORKED_ENTITY);

		switch (networkComp->m_entityType) {
		case Netcode::EntityType::PLAYER_ENTITY:
		{
			networkComp->addMessageType(Netcode::MessageType::CHANGE_LOCAL_POSITION);
			networkComp->addMessageType(Netcode::MessageType::CHANGE_LOCAL_ROTATION);

		}
		break;
		case Netcode::EntityType::CANDLE_ENTITY:
		{
			networkComp->addMessageType(Netcode::MessageType::CHANGE_ABSOLUTE_POS_AND_ROT);
		}
		break;
		default:
			Logger::Error("INVALID EntityType in writeMessageToArchive::CREATE_NETWORKED_ENTITY");
			break;
		}
	}
	break;
	case Netcode::MessageType::CHANGE_LOCAL_POSITION:
	{
		TransformComponent* t = e->getComponent<TransformComponent>();
		ArchiveHelpers::saveVec3(ar, t->getTranslation());
	}
	break;
	case Netcode::MessageType::CHANGE_LOCAL_ROTATION:
	{
		TransformComponent* t = e->getComponent<TransformComponent>();
		ArchiveHelpers::saveVec3(ar, t->getRotations());
	}
	break;

	case Netcode::MessageType::CHANGE_ABSOLUTE_POS_AND_ROT:
	{
		glm::vec3 scale;
		glm::quat rotation;
		glm::vec3 translation;
		glm::vec3 skew;
		glm::vec4 perspective;

		TransformComponent* t = e->getComponent<TransformComponent>();
		//ArchiveHelpers::saveMat4(ar, t->getMatrixWithUpdate());

		glm::decompose(t->getMatrixWithUpdate(), scale, rotation, translation, skew, perspective);

		ArchiveHelpers::saveVec3(ar, translation);
		ArchiveHelpers::saveQuat(ar, rotation);
	}
	break;
	case Netcode::MessageType::ANIMATION:
	{
		ar(e->getComponent<AnimationComponent>()->animationIndex);
		ar(e->getComponent<AnimationComponent>()->animationTime);
	}
	break;
	case Netcode::MessageType::SHOOT_START:
	{
		// Only do this once
		e->getComponent<NetworkSenderComponent>()->removeMessageType(Netcode::MessageType::SHOOT_START);

		// Send data to others
		GunComponent* g = e->getComponent<GunComponent>();
		ArchiveHelpers::saveVec3(ar, g->position);
		ArchiveHelpers::saveVec3(ar, g->projectileSpeed * g->direction); // Velocity

		// Transition into loop
		e->getComponent<NetworkSenderComponent>()->addMessageType(Netcode::MessageType::SHOOT_LOOP);
	}
	break;
	case Netcode::MessageType::SHOOT_LOOP:
	{
		// Send data to others
		GunComponent* g = e->getComponent<GunComponent>();
		ArchiveHelpers::saveVec3(ar, g->position);
		ArchiveHelpers::saveVec3(ar, g->projectileSpeed * g->direction); // Velocity
	}
	break;
	case Netcode::MessageType::SHOOT_END:
	{
		// Only do this once
		e->getComponent<NetworkSenderComponent>()->removeMessageType(Netcode::MessageType::SHOOT_END);
		// Stop looping
		e->getComponent<NetworkSenderComponent>()->removeMessageType(Netcode::MessageType::SHOOT_LOOP);

		// Send data to others
		GunComponent* g = e->getComponent<GunComponent>();
		ArchiveHelpers::saveVec3(ar, g->position);
		ArchiveHelpers::saveVec3(ar, g->projectileSpeed * g->direction); // Velocity
	}
	break;
	default:
		Logger::Error("TRIED TO SEND INVALID NETWORK MESSAGE (" + std::to_string((int)messageType));
		break;
	}
}

void NetworkSenderSystem::writeEventToArchive(NetworkSenderEvent* event, Netcode::OutArchive& ar) {
	ar(event->type); // Send the event-type
#ifdef DEVELOPMENT
	ar(event->REDUNDANT_TYPE);
#endif

	switch (event->type) {
	case Netcode::MessageType::SPAWN_PROJECTILE:
	{
		Netcode::MessageSpawnProjectile* data = static_cast<Netcode::MessageSpawnProjectile*>(event->data);

		ArchiveHelpers::saveVec3(ar, data->translation);
		ArchiveHelpers::saveVec3(ar, data->velocity);
		ar(data->ownerPlayerComponentID);
	}
	break;
	case Netcode::MessageType::PLAYER_JUMPED:
	{
		Netcode::MessagePlayerJumped* data = static_cast<Netcode::MessagePlayerJumped*>(event->data);

		ar(data->playerWhoJumped);
	}
	break;
	case Netcode::MessageType::PLAYER_LANDED:
	{
		Netcode::MessagePlayerLanded* data = static_cast<Netcode::MessagePlayerLanded*>(event->data);

		ar(data->playerWhoLanded);
	}
	break;
	case Netcode::MessageType::WATER_HIT_PLAYER:
	{
		Netcode::MessageWaterHitPlayer* data = static_cast<Netcode::MessageWaterHitPlayer*>(event->data);

		ar(data->playerWhoWasHitID);
	}
	break;
	case Netcode::MessageType::PLAYER_DIED:
	{
		Netcode::MessagePlayerDied* data = static_cast<Netcode::MessagePlayerDied*>(event->data);

		ar(data->playerWhoDied); // Send
		ar(data->playerWhoFired);
	}
	break;
	case Netcode::MessageType::PLAYER_DISCONNECT:
	{
		Netcode::MessagePlayerDisconnect* data = static_cast<Netcode::MessagePlayerDisconnect*>(event->data);

		ar(data->playerID); // Send
	}
	break;
	case Netcode::MessageType::MATCH_ENDED:
	{

	}
	break;
	case Netcode::MessageType::SEND_ALL_BACK_TO_LOBBY:
	{

	}
	break;
	case Netcode::MessageType::RUNNING_METAL_START:
	{
		Netcode::MessageRunningMetalStart* data = static_cast<Netcode::MessageRunningMetalStart*>(event->data);
		ar(data->runningPlayer); // Send
	}
	break;
	case Netcode::MessageType::RUNNING_TILE_START:
	{
		Netcode::MessageRunningTileStart* data = static_cast<Netcode::MessageRunningTileStart*>(event->data);
		ar(data->runningPlayer); // Send
	}
	break;
	case Netcode::MessageType::RUNNING_STOP_SOUND:
	{
		Netcode::MessageRunningStopSound* data = static_cast<Netcode::MessageRunningStopSound*>(event->data);
		ar(data->runningPlayer); // Send
	}
	break;
	case Netcode::MessageType::CANDLE_HELD_STATE:
	{
		Netcode::MessageCandleHeldState* data = static_cast<Netcode::MessageCandleHeldState*>(event->data);

		ar(data->candleOwnerID);
		ar(data->isHeld);
		ArchiveHelpers::saveVec3(ar, data->candlePos);
	}
	break;
	case Netcode::MessageType::ENDGAME_STATS:
	{
		GameDataTracker* dgtp = &GameDataTracker::getInstance();
		std::map<Netcode::PlayerID, HostStatsPerPlayer> tmpPlayerMap = dgtp->getPlayerDataMap();
		// Send player count to clients for them to loop following data
		ar(tmpPlayerMap.size());

		// Send all per player data. Match this on the reciever end
		for (auto player = tmpPlayerMap.begin(); player != tmpPlayerMap.end(); ++player) {
			ar(player->first);
			ar(player->second.nKills);
			ar(player->second.placement);
		}

		// Send all specific data. The host has processed data from all clients and will 
		// now return it to their endscreens.
		ar(dgtp->getStatisticsGlobal().bulletsFired);
		ar(dgtp->getStatisticsGlobal().bulletsFiredID);

		ar(dgtp->getStatisticsGlobal().distanceWalked);
		ar(dgtp->getStatisticsGlobal().distanceWalkedID);

		ar(dgtp->getStatisticsGlobal().jumpsMade);
		ar(dgtp->getStatisticsGlobal().jumpsMadeID);


	}
	break;
	case Netcode::MessageType::PREPARE_ENDSCREEN:
	{
		// Send all specific data to Host
		ar(GameDataTracker::getInstance().getStatisticsLocal().bulletsFired);
		ar(GameDataTracker::getInstance().getStatisticsLocal().distanceWalked);
		ar(GameDataTracker::getInstance().getStatisticsLocal().jumpsMade);
		
	}
	break;
	case Netcode::MessageType::IGNITE_CANDLE:
	{
		Netcode::MessageIgniteCandle* data = static_cast<Netcode::MessageIgniteCandle*>(event->data);
		ar(data->candleOwnerID);
	}
	break;
	default:
		break;
	}
}