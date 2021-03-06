/*
This source file is part of Rigs of Rods
Copyright 2005-2012 Pierre-Michel Ricordel
Copyright 2007-2012 Thomas Fischer

For more information, see http://www.rigsofrods.com/

Rigs of Rods is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3, as
published by the Free Software Foundation.

Rigs of Rods is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Rigs of Rods.  If not, see <http://www.gnu.org/licenses/>.
*/

// created by Thomas Fischer thomas{AT}thomasfischer{DOT}biz, 24th of August 2009

#include "BeamFactory.h"

#include "BeamEngine.h"
#include "collisions.h"
#include "network.h"
#include "RoRFrameListener.h"
#include "Settings.h"
#include "SoundScriptManager.h"

#ifdef USE_MYGUI
#include "gui_mp.h"
#include "gui_menu.h"
#include "DashBoardManager.h"
#endif // USE_MYGUI

using namespace Ogre;


template<> BeamFactory *StreamableFactory < BeamFactory, Beam >::_instance = 0;

BeamFactory::BeamFactory(SceneManager *manager, SceneNode *parent, RenderWindow* win, Network *net, float *mapsizex, float *mapsizez, Collisions *icollisions, HeightFinder *mfinder, Water *w, Camera *pcam) :
	  manager(manager)
	, parent(parent)
	, win(win)
	, net(net)
	, mapsizex(mapsizex)
	, mapsizez(mapsizez)
	, icollisions(icollisions)
	, mfinder(mfinder)
	, w(w)
	, pcam(pcam)
	, current_truck(-1)
	, free_truck(0)
	, physFrame(0)
	, tdr(0)
{
	for (int t=0; t < MAX_TRUCKS; t++)
		trucks[t] = 0;

	if (BSETTING("Multi-threading", true))
		Beam::thread_mode = THREAD_MULTI;

	if (BSETTING("2DReplay", false))
		tdr = new TwoDReplay();
}

BeamFactory::~BeamFactory()
{
}

Beam *BeamFactory::createLocal(int slotid)
{
	// do not use this ...
	return 0;
}

bool BeamFactory::removeBeam(Beam *b)
{
	lockStreams();
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
	std::map < int, std::map < unsigned int, Beam *> >::iterator it1;
	std::map < unsigned int, Beam *>::iterator it2;

	for (it1=streamables.begin(); it1!=streamables.end(); it1++)
	{
		for (it2=it1->second.begin(); it2!=it1->second.end(); it2++)
		{
			if (it2->second == b)
			{
				NetworkStreamManager::getSingleton().removeStream(it1->first, it2->first);
				_deleteTruck(it2->second);
				it1->second.erase(it2);
				unlockStreams();
				return true;
			}
		}
	}
	unlockStreams();

#ifdef USE_MYGUI
	GUI_MainMenu::getSingleton().triggerUpdateVehicleList();
#endif // USE_MYGUI

	return false;
}

Beam *BeamFactory::createLocal(Vector3 pos, Quaternion rot, String fname, collision_box_t *spawnbox, bool ismachine, int flareMode, std::vector<String> *truckconfig, Skin *skin, bool freePosition)
{
	int truck_num = getFreeTruckSlot();
	if (truck_num == -1)
	{
		LOG("ERROR: Could not add beam to main list");
		return 0;
	}

	Beam *b = new Beam(
		truck_num,
		manager,
		manager->getRootSceneNode()->createChildSceneNode(),
		win,
		net,
		mapsizex,
		mapsizez,
		pos.x,
		pos.y,
		pos.z,
		rot,
		fname.c_str(),
		icollisions,
		mfinder,
		w,
		pcam,
		false, // networked
		net!=0, // networking
		spawnbox,
		ismachine,
		flareMode,
		truckconfig,
		skin,
		freePosition);

	trucks[truck_num] = b;

	// lock slide nodes after spawning the truck?
	if (b->getSlideNodesLockInstant())
	{
		b->toggleSlideNodeLock();
	}

	lockStreams();
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
	streamables[-1][10 + truck_num] = b; // 10 streams offset for beam constructions
	unlockStreams();

#ifdef USE_MYGUI
	GUI_MainMenu::getSingleton().triggerUpdateVehicleList();
#endif // USE_MYGUI

	// add own username to truck
	if (net)
	{
		b->updateNetworkInfo();
	}

	return b;
}

Beam *BeamFactory::createRemoteInstance(stream_reg_t *reg)
{
	// NO LOCKS IN HERE, already locked

	stream_register_trucks_t *treg = (stream_register_trucks_t *)&reg->reg;

	LOG(" new beam truck for " + TOSTRING(reg->sourceid) + ":" + TOSTRING(reg->streamid));

#ifdef USE_SOCKTEW
	// log a message about this
	if (net)
	{
		client_t *c = net->getClientInfo(reg->sourceid);
		if (c)
		{
			UTFString username = ChatSystem::getColouredName(*c);
			UTFString message = username + ChatSystem::commandColour + _L(" spawned a new vehicle: ") + ChatSystem::normalColour + treg->name;
#ifdef USE_MYGUI
			Console *console = Console::getSingletonPtrNoCreation();
			if (console) console->putMessage(Console::CONSOLE_MSGTYPE_NETWORK, Console::CONSOLE_VEHILCE_ADD, message, "car_add.png");
#endif // USE_MYGUI
		}
	}
#endif // USE_SOCKETW

	// check if we got this truck installed
	String filename = String(treg->name);
	String group = "";
	if (!CACHE.checkResourceLoaded(filename, group))
	{
		LOG("wont add remote stream (truck not existing): '"+filename+"'");

		// add 0 to the map so we know its stream is existing but not usable for us
		// already locked
		//lockStreams();
		std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
		streamables[reg->sourceid][reg->streamid] = 0;
		//unlockStreams();

		return 0;
	}

	// fill truckconfig
	std::vector<String> truckconfig;
	for(int t=0; t<10; t++)
	{
		if (!strnlen(treg->truckconfig[t], 60))
			break;
		truckconfig.push_back(String(treg->truckconfig[t]));
	}


	// DO NOT spawn the truck far off anywhere
	// the truck parsing will break flexbodies initialization when using huge numbers here
	Vector3 pos = Vector3::ZERO;

	int truck_num = getFreeTruckSlot();
	if (truck_num == -1)
	{
		LOG("ERROR: could not add beam to main list");
		return 0;
	}

	Beam *b = new Beam(
		truck_num,
		manager,
		manager->getRootSceneNode(),
		win,
		net,
		mapsizex,
		mapsizez,
		pos.x,
		pos.y,
		pos.z,
		Quaternion::ZERO,
		reg->reg.name,
		icollisions,
		mfinder,
		w,
		pcam,
		true, // networked
		net!=0, // networking
		0,
		false,
		3,
		&truckconfig,
		0);

	trucks[truck_num] = b;

	b->setSourceID(reg->sourceid);
	b->setStreamID(reg->streamid);

	// already locked
	//lockStreams();
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
	streamables[reg->sourceid][reg->streamid] = b;
	//unlockStreams();

	b->updateNetworkInfo();

#ifdef USE_MYGUI
	GUI_MainMenu::getSingleton().triggerUpdateVehicleList();
#endif // USE_MYGUI

	return b;
}

void BeamFactory::localUserAttributesChanged(int new_id)
{
	lockStreams();
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();

	if (streamables.find(-1) != streamables.end())
	{
		//Beam *b = streamables[-1][0];
		streamables[new_id][0] = streamables[-1][0]; // add alias :)
		//b->setUID(newid);
		//b->updateNetLabel();
	}
	unlockStreams();
}

void BeamFactory::netUserAttributesChanged(int source_id, int stream_id)
{
	lockStreams();
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
	std::map < int, std::map < unsigned int, Beam *> >::iterator it_source = streamables.find(source_id);
	std::map < unsigned int, Beam *>::iterator it_stream;
	
	if (it_source != streamables.end() && !it_source->second.empty())
	{
		it_stream = it_source->second.find(stream_id);
		if (it_stream != it_source->second.end() && it_stream->second)
			it_stream->second->updateNetworkInfo();
	}
	unlockStreams();
}

Beam *BeamFactory::getBeam(int source_id, int stream_id)
{
	lockStreams();
	Beam *retVal = 0;
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
	std::map < int, std::map < unsigned int, Beam *> >::iterator it_source = streamables.find(source_id);
	std::map < unsigned int, Beam *>::iterator it_stream;

	if (it_source != streamables.end() && !it_source->second.empty())
	{
		it_stream = it_source->second.find(stream_id);
		if (it_stream != it_source->second.end() && it_stream->second)
			retVal = it_stream->second;
	}
	unlockStreams();
	return retVal;
}

bool BeamFactory::syncRemoteStreams()
{
	// we override this here, so we know if something changed and could update the player list
	// we delete and add trucks in there, so be sure that nothing runs as we delete them ...
	bool changes = StreamableFactory <BeamFactory, Beam>::syncRemoteStreams();
	
	if (changes)
		updateGUI();

	return changes;
}

void BeamFactory::updateGUI()
{
#ifdef USE_MYGUI
#ifdef USE_SOCKETW
	GUI_Multiplayer::getSingleton().update();
#endif // USE_SOCKETW
#endif // USE_MYGUI	
}

// j is the index of a MAYSLEEP truck, returns true if one active was found in the set
bool BeamFactory::checkForActive(int j, std::bitset<MAX_TRUCKS> &sleepy)
{
	sleepy.set(j, true);
	for (int t=0; t < free_truck; t++)
	{
		if (trucks[t] && !sleepy[t] &&
			((trucks[j]->minx<trucks[t]->minx && trucks[t]->minx<trucks[j]->maxx) || (trucks[j]->minx<trucks[t]->maxx && trucks[t]->maxx<trucks[j]->maxx) || (trucks[t]->minx<trucks[j]->maxx && trucks[j]->maxx<trucks[t]->maxx)) &&
			((trucks[j]->miny<trucks[t]->miny && trucks[t]->miny<trucks[j]->maxy) || (trucks[j]->miny<trucks[t]->maxy && trucks[t]->maxy<trucks[j]->maxy) || (trucks[t]->miny<trucks[j]->maxy && trucks[j]->maxy<trucks[t]->maxy)) &&
			((trucks[j]->minz<trucks[t]->minz && trucks[t]->minz<trucks[j]->maxz) || (trucks[j]->minz<trucks[t]->maxz && trucks[t]->maxz<trucks[j]->maxz) || (trucks[t]->minz<trucks[j]->maxz && trucks[j]->maxz<trucks[t]->maxz)))
		{
			if (trucks[t]->state == SLEEPING || trucks[t]->state == MAYSLEEP || trucks[t]->state == GOSLEEP || (trucks[t]->state == DESACTIVATED && trucks[t]->sleepcount >= 5))
				return checkForActive(t, sleepy);
			else
				return true;
		}
	}
	return false;
}

void BeamFactory::recursiveActivation(int j)
{
	for (int t=0; t < free_truck; t++)
	{
		if (!trucks[t]) continue;
		if ((trucks[t]->state == SLEEPING || trucks[t]->state == MAYSLEEP || trucks[t]->state == GOSLEEP || (trucks[t]->state == DESACTIVATED && trucks[t]->sleepcount >= 5)) &&
			((trucks[j]->minx<trucks[t]->minx && trucks[t]->minx<trucks[j]->maxx) || (trucks[j]->minx<trucks[t]->maxx && trucks[t]->maxx<trucks[j]->maxx) || (trucks[t]->minx<trucks[j]->maxx && trucks[j]->maxx<trucks[t]->maxx)) &&
			((trucks[j]->miny<trucks[t]->miny && trucks[t]->miny<trucks[j]->maxy) || (trucks[j]->miny<trucks[t]->maxy && trucks[t]->maxy<trucks[j]->maxy) || (trucks[t]->miny<trucks[j]->maxy && trucks[j]->maxy<trucks[t]->maxy)) &&
			((trucks[j]->minz<trucks[t]->minz && trucks[t]->minz<trucks[j]->maxz) || (trucks[j]->minz<trucks[t]->maxz && trucks[t]->maxz<trucks[j]->maxz) || (trucks[t]->minz<trucks[j]->maxz && trucks[j]->maxz<trucks[t]->maxz)))
		{
			trucks[t]->desactivate(); // make the truck not leading but active
			trucks[t]->disableDrag = trucks[current_truck]->driveable==AIRPLANE;
			recursiveActivation(t);
		}
	}
}

void BeamFactory::checkSleepingState()
{
	if (current_truck >= 0 && trucks[current_truck])
	{
		trucks[current_truck]->disableDrag = false;
		recursiveActivation(current_truck);
		//if its grabbed, its moving
		//if (isnodegrabbed && trucks[truckgrabbed]->state==SLEEPING) trucks[truckgrabbed]->desactivate();
		// put to sleep
		for (int t=0; t < free_truck; t++)
		{
			if (trucks[t] && trucks[t]->state == MAYSLEEP)
			{
				std::bitset<MAX_TRUCKS> sleepy;
				if (!checkForActive(t, sleepy))
				{
					// no active truck in the set, put everybody to sleep
					for (int i=0; i < free_truck; i++)
					{
						if (trucks[i] && sleepy[i])
						{
							trucks[i]->state=GOSLEEP;
						}
					}
				}
			}
		}

		/* obsolete for now
		// special stuff for rollable gear
		int t;
		bool rollmode=false;
		for (t=0; t < free_truck; t++)
		{
			if (!trucks[t]) continue;
			if (trucks[t]->state != SLEEPING)
				rollmode = rollmode || trucks[t]->wheel_contact_requested;
			
			trucks[t]->requires_wheel_contact = rollmode;// && !trucks[t]->wheel_contact_requested;
		}
		//*/
	}
}

int BeamFactory::getFreeTruckSlot()
{
	// find a free slot for the truck
	for (int t=0; t<MAX_TRUCKS; t++)
	{
		if (!trucks[t] && t >= free_truck) // XXX: TODO: remove this hack
		{
			// reuse slots
			if (t >= free_truck)
				free_truck = t + 1;
			return t;
		}
	}
	return -1;
}

void BeamFactory::activateAllTrucks()
{
	for (int t=0; t < free_truck; t++)
	{
		if (trucks[t] && trucks[t]->state >= DESACTIVATED || trucks[t]->state <= SLEEPING)
		{
			trucks[t]->desactivate(); // make the truck not leading but active
			trucks[t]->disableDrag = trucks[current_truck]->driveable==AIRPLANE;
			recursiveActivation(t);
		}
	}
}

void BeamFactory::sendAllTrucksSleeping()
{
	for (int t=0; t < free_truck; t++)
	{
		if (trucks[t] && trucks[t]->state == ACTIVATED)
		{
			trucks[t]->state = GOSLEEP;
		}
	}
}

void BeamFactory::recalcGravityMasses()
{
	// update the mass of all trucks
	for (int t=0; t < free_truck; t++)
	{
		if (trucks[t])
		{
			trucks[t]->recalc_masses();
		}
	}
}

int BeamFactory::findTruckInsideBox(Collisions *collisions, char* inst, char* box)
{
	// try to find the desired truck (the one in the box)
	int id = -1;
	for (int t=0; t < free_truck; t++)
	{
		if (!trucks[t]) continue;
		if (collisions->isInside(trucks[t]->nodes[0].AbsPosition, inst, box))
		{
			if (id == -1)
				// first truck found
				id = t;
			else
				// second truck found -> unclear which vehicle was meant
				return -1;
		}
	}
	return id;
}

void BeamFactory::repairTruck(Collisions *collisions, char* inst, char* box, bool keepPosition)
{
	int rtruck = findTruckInsideBox(collisions, inst, box);
	if (rtruck >= 0)
	{
		// take a position reference
#ifdef USE_OPENAL
		SoundScriptManager::getSingleton().trigOnce(rtruck, SS_TRIG_REPAIR);
#endif // USE_OPENAL
		Vector3 ipos=trucks[rtruck]->nodes[0].AbsPosition;
		trucks[rtruck]->reset();
		trucks[rtruck]->resetPosition(ipos.x, ipos.z, false);
		trucks[rtruck]->updateVisual();
	}
}

void BeamFactory::removeTruck(Collisions *collisions, char* inst, char* box)
{
	int rtruck = findTruckInsideBox(collisions, inst, box);

	if (rtruck >= 0)
	{
		removeTruck(rtruck);
	}
}

void BeamFactory::removeTruck(int truck)
{
	if (truck < 0 || truck > free_truck)
		return;

	if (current_truck == truck)
		setCurrentTruck(-1);

	if (!removeBeam(trucks[truck]))
		// deletion over beamfactory failed, delete by hand
		// then delete the class
		_deleteTruck(trucks[truck]);
}

void BeamFactory::_deleteTruck(Beam *b)
{
	if (b == 0)	return;

	trucks[b->trucknum] = 0;
	delete b;
	b = 0;

#ifdef USE_MYGUI
	GUI_MainMenu::getSingleton().triggerUpdateVehicleList();
#endif // USE_MYGUI
}

void BeamFactory::removeCurrentTruck()
{
	removeTruck(current_truck);
}

void BeamFactory::setCurrentTruck(int new_truck)
{
	if (current_truck >= 0 && current_truck < free_truck && trucks[current_truck])
		trucks[current_truck]->desactivate();

	int previous_truck = current_truck;
	current_truck = new_truck;

	if (RoRFrameListener::eflsingleton)
	{
		if (previous_truck >= 0 && current_truck >= 0)
			RoRFrameListener::eflsingleton->changedCurrentTruck(trucks[previous_truck], trucks[current_truck]);
		else if (previous_truck >= 0)
			RoRFrameListener::eflsingleton->changedCurrentTruck(trucks[previous_truck], 0);
		else if (current_truck >= 0)
			RoRFrameListener::eflsingleton->changedCurrentTruck(0, trucks[current_truck]);
		else
			RoRFrameListener::eflsingleton->changedCurrentTruck(0, 0);
	}
}

bool BeamFactory::enterRescueTruck()
{
	// rescue!
	// search a rescue truck
	for (int t=0; t < free_truck; t++)
	{
		if (trucks[t] && trucks[t]->rescuer) {
			// go to person mode first
			setCurrentTruck(-1);
			// then to the rescue truck, this fixes overlapping interfaces
			setCurrentTruck(t);
			return true;
		}
	}
	return false;
}

void BeamFactory::updateVisual(float dt)
{
	for (int t=0; t < free_truck; t++)
	{
		if (!trucks[t]) continue;
		
		// always update the labels
		trucks[t]->updateLabels(dt);

		if (trucks[t]->state != SLEEPING && trucks[t]->loading_finished)
		{
			trucks[t]->updateSkidmarks();
			trucks[t]->updateVisual(dt);
			trucks[t]->updateFlares(dt, (t==current_truck) );
		}
	}
}

void BeamFactory::updateAI(float dt)
{
	for (int t=0; t < free_truck; t++)
	{
		if (trucks[t])
		{
			trucks[t]->updateAI(dt);
		}
	}
}


void BeamFactory::calcPhysics(float dt)
{
	physFrame++;

	if (current_truck >= 0 && current_truck < free_truck)
		trucks[current_truck]->frameStep(dt);

	// update 2D replay if activated
	if (tdr) tdr->update(dt);

	// things always on
	for (int t=0; t < free_truck; t++)
	{
		if (!trucks[t]) continue;

		// networked trucks must be taken care of
		switch(trucks[t]->state)
		{
			case NETWORKED:
			{
				trucks[t]->calcNetwork();
				break;
			}
			case RECYCLE:
			{
				break;
			}
			default:
			{
				if (t != current_truck && trucks[t]->engine)
					trucks[t]->engine->update(dt, 1);
				if (trucks[t]->networking)
					trucks[t]->sendStreamData();
			}
			break;
		}
	}
}

void BeamFactory::removeInstance(Beam *b)
{
	if (b == 0) return;
	// hide the truck
	b->deleteNetTruck();
	//_deleteTruck(b);
}

void BeamFactory::removeInstance(stream_del_t *del)
{
	// we override this here so we can also delete the truck array content
	// already locked
	// lockStreams();
	std::map < int, std::map < unsigned int, Beam *> > &streamables = getStreams();
	std::map < int, std::map < unsigned int, Beam *> >::iterator it_stream = streamables.find(del->sourceid);;
	std::map < unsigned int, Beam *>::iterator it_beam;

	if (it_stream == streamables.end() || it_stream->second.empty())
		// no stream for this source id
		return;

	if (del->streamid == -1)
	{
		// delete all streams
		for(it_beam=it_stream->second.begin(); it_beam != it_stream->second.end(); it_beam++)
			removeInstance(it_beam->second);
	} else
	{
		// find the stream matching the streamid
		it_beam = it_stream->second.find(del->streamid);
		if (it_beam != it_stream->second.end())
			removeInstance(it_beam->second);
	}
	// unlockStreams();
}

void BeamFactory::windowResized()
{
#ifdef USE_MYGUI
	for(int t=0; t < free_truck; t++)
	{
		if(trucks[t])
		{
			trucks[t]->dash->windowResized();
		}
	}
#endif // USE_MYGUI
}
