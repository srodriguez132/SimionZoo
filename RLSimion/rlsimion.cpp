// RLSimion-Lib-test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "../RLSimion-Lib/world.h"
#include "../RLSimion-Lib/simgod.h"
#include "../RLSimion-Lib/reward.h"
#include "../RLSimion-Lib/states-and-actions.h"
#include "../RLSimion-Lib/globals.h"
#include "../RLSimion-Lib/experiment.h"


int main(int argc, char* argv[])
{
	tinyxml2::XMLDocument xmlDoc;
	tinyxml2::XMLElement* pParameters;

	
	//CParameters *pParameters;
	if (argc > 1)
		xmlDoc.LoadFile(argv[1]);
		//pParameters = new CParameters(argv[1]);
	else
	{
		printf("ERROR: configuration file not provided as an argument");
		exit(-1);
	}
	printf("\n\n******************\nRLSimion\n******************\nConfig. file %s\n******************\n\n", argv[1]);

	pParameters = xmlDoc.FirstChildElement("RLSimion");
	if (xmlDoc.Error())
	{
		printf("Error loading configuration file: %s\n\n", xmlDoc.ErrorName());
		exit(-1);
	}
	RLSimion::g_pWorld = new CWorld(pParameters->FirstChildElement("World"));
	CSimGod* pSimGod = new CSimGod(pParameters->FirstChildElement("SimGod"));
	RLSimion::g_pExperiment = new CExperiment(pParameters->FirstChildElement("Experiment"));
	//CParameterScheduler* pParameterScheduler= new CParameterScheduler(pParameters->getChild("PARAMETER_SCHEDULER"));

	CState *s= RLSimion::g_pWorld->getStateInstance();
	CState *s_p= RLSimion::g_pWorld->getStateInstance();
	CAction *a= RLSimion::g_pWorld->getActionInstance();

	double r= 0.0;
	double td= 0.0;

	//episodes
	for (RLSimion::g_pExperiment->m_expProgress.setEpisode(1); RLSimion::g_pExperiment->m_expProgress.isValidEpisode(); RLSimion::g_pExperiment->m_expProgress.incEpisode())
	{
		RLSimion::g_pWorld->reset(s);

		//steps per episode
		for (RLSimion::g_pExperiment->m_expProgress.setStep(1); RLSimion::g_pExperiment->m_expProgress.isValidStep(); RLSimion::g_pExperiment->m_expProgress.incStep())
		{
			//a= pi(s)
			pSimGod->selectAction(s,a);

			//s_p= f(s,a); r= R(s');
			r= RLSimion::g_pWorld->executeAction(s,a,s_p);

			//update god's policy and value estimation
			pSimGod->update(s, a, s_p, r);

			//log tuple <s,a,s',r>
			RLSimion::g_pExperiment->logStep(s,a,s_p,RLSimion::g_pWorld->getReward()); //we need the complete reward vector for logging

			//s= s'
			s->copy(s_p);
		}
	}

	delete pSimGod;


	delete s;
	delete s_p;
	delete a;

	delete RLSimion::g_pWorld;
	delete RLSimion::g_pExperiment;

	return 0;
}