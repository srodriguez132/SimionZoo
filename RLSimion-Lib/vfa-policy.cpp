#include "stdafx.h"
#include "named-var-set.h"
#include "noise.h"
#include "vfa.h"
#include "policy-learner.h"
#include "parameters.h"
#include "globals.h"
#include "world.h"
#include "features.h"
#include "policy.h"

CLASS_FACTORY(CDeterministicPolicy)
{
	CHOICE("Policy","The policy type");

	CHOICE_ELEMENT("Deterministic-Policy-Gaussian-Noise", CDeterministicPolicyGaussianNoise,"A deterministic polich pi(s) to which some noise is added");
	CHOICE_ELEMENT("Stochastic-Policy-Gaussian-Noise", CStochasticPolicyGaussianNoise,"An stochastic policy pi(s)= N(pi_mean(s),pi_variance(s))");

	END_CHOICE();
	END_CLASS();
	return 0;
}

CLASS_CONSTRUCTOR(CDeterministicPolicy)
: CParamObject(pParameters)
{
	CHILD_CLASS_FACTORY(m_pVFA, "Linear-State-VFA", "The parameterized VFA that approximates the function", "", CLinearStateVFA);
	
	ACTION_VARIABLE_REF(m_outputActionIndex, "Output-Action","The output action variable");

	CAction* pActionDescriptor = CWorld::getDynamicModel()->getActionDescriptor();
	m_pVFA->saturateOutput(pActionDescriptor->getMin(m_outputActionIndex), pActionDescriptor->getMax(m_outputActionIndex));
	END_CLASS();
}

CDeterministicPolicy::~CDeterministicPolicy()
{
	delete m_pVFA;
}



//CDetPolicyGaussianNoise////////////////////////////////
/////////////////////////////////////////////////////////

CLASS_CONSTRUCTOR(CDeterministicPolicyGaussianNoise)
	: EXTENDS(CDeterministicPolicy, pParameters)
{
	CHILD_CLASS_FACTORY(m_pExpNoise,"Exploration-Noise","Parameters of the noise used as exploration","",CNoise);
	END_CLASS();
}

CDeterministicPolicyGaussianNoise::~CDeterministicPolicyGaussianNoise()
{
	delete m_pExpNoise;
}

void CDeterministicPolicyGaussianNoise::getGradient(const CState* s, const CAction* a, CFeatureList* pOutGradient)
{
	//0. Grad_u pi(a|s)/pi(a|s) = (a - pi(s)) * phi(s) / sigma*2
	m_pVFA->getFeatures(s, pOutGradient);

	//TXAPUZAAAA^2!!!
	double sigma = ((CGaussianNoise*)m_pExpNoise)->getSigma();

	double noise = a->getValue(m_outputActionIndex) - m_pVFA->getValue((const CFeatureList*)pOutGradient);

	double unscaled_noise = ((CGaussianNoise*)m_pExpNoise)->unscale(noise);
	double factor = unscaled_noise / (sigma*sigma);
}

void CDeterministicPolicyGaussianNoise::selectAction(const CState *s, CAction *a)
{
	double noise;
	double output;

	noise = m_pExpNoise->getValue();

	output = m_pVFA->getValue(s);

	a->setValue(m_outputActionIndex, output + noise);
}

//CStoPolicyGaussianNoise//////////////////////////
////////////////////////////////////////////////

CLASS_CONSTRUCTOR(CStochasticPolicyGaussianNoise)
	: EXTENDS(CDeterministicPolicy, pParameters)
{
	CHILD_CLASS_FACTORY(m_pSigmaVFA, "Sigma-VFA", "The parameterized VFA that approximates variance(s)", "", CLinearStateVFA);
	//m_pSigmaVFA = new CLinearStateVFA(m_pVFA->getParameters());//same parameterization as the mean-VFA
	m_pAux = new CFeatureList("Sto-Policy/aux");
	END_CLASS();
}

CStochasticPolicyGaussianNoise::~CStochasticPolicyGaussianNoise()
{
	delete m_pSigmaVFA;
	delete m_pAux;
}

void CStochasticPolicyGaussianNoise::selectAction(const CState *s, CAction *a)
{
	double sigma;
	double mean;
	double output;

	sigma = m_pSigmaVFA->getValue(s);

	mean = m_pVFA->getValue(s);

	output = getNormalDistributionSample(mean, sigma);

	a->setValue(m_outputActionIndex, output);
}

//returns the factor by which the state features have to be multiplied to get the policy gradient
void CStochasticPolicyGaussianNoise::getGradient(const CState* s, const CAction* a, CFeatureList* pOutGradient)
{

}