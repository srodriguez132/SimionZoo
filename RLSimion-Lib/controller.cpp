#include "stdafx.h"
#include "controller.h"
#include "globals.h"
#include "named-var-set.h"
#include "world.h"
#include "config.h"
#include "globals.h"
#include "parameters-numeric.h"
#include "app.h"

CLASS_FACTORY(CController)
{
	CHOICE("Controller","The specific controller to be used");
	CHOICE_ELEMENT("PID", CPIDController,"A PID controller");
	CHOICE_ELEMENT("LQR", CLQRController,"An LQR controller");
	CHOICE_ELEMENT("Jonkman", CWindTurbineJonkmanController, "The Jonkman wind-turbine controller. Outputs: d_T_g and d_beta");
	CHOICE_ELEMENT("Vidal", CWindTurbineVidalController, "The Vidal wind-turbine controller. Outputs: d_T_g and d_beta");
	CHOICE_ELEMENT("Boukhezzar", CWindTurbineBoukhezzarController, "The Boukhezzar wind-turbine controller. Outputs: d_T_g and d_beta");
	END_CHOICE();

	return 0;

	END_CLASS();
}



//LQR//////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
	/*int* m_pVariableIndices;
	double *m_pGains;
	int numVars;*/

CLASS_CONSTRUCTOR(CLQRGain)
{
	STATE_VARIABLE_REF(m_variableIndex, "Variable", "The input state variable");
	CONST_DOUBLE_VALUE(m_gain, "Gain", 0.0,"The gain applied to the input state variable");
	
	END_CLASS();
}
CLASS_CONSTRUCTOR(CLQRController)
{
	ACTION_VARIABLE_REF(m_outputActionIndex,"Output-Action","The output action");
	
	MULTI_VALUED(m_gains, "LQR-Gain", "An LQR gain on an input state variable", CLQRGain);
	

	END_CLASS();
}

CLQRController::~CLQRController()
{
	for (unsigned int i = 0; i < m_gains.size(); i++) delete m_gains[i];
}

void CLQRController::selectAction(const CState *s, CAction *a)
{
	double output= 0.0; // only 1-dimension so far

	for (unsigned int i= 0; i<m_gains.size(); i++)
	{
		output+= s->getValue(m_gains[i]->m_variableIndex)*m_gains[i]->m_gain;
	}
	// delta= -K*x
	a->setValue(m_outputActionIndex, -output);
}

int CLQRController::getNumOutputs()
{
	return 1;
}
int CLQRController::getOutputActionIndex(int output)
{
	if (output == 0)
		return m_outputActionIndex;
	throw std::exception("CLQRController. Invalid action output given.");
	return -1;
}

//PID//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

CLASS_CONSTRUCTOR(CPIDController)
{
	ACTION_VARIABLE_REF(m_outputActionIndex, "Output-Action","The output action");

	NUMERIC_VALUE(m_pKP,"KP", "Proportional gain");
	NUMERIC_VALUE(m_pKI, "KI","Integral gain");
	NUMERIC_VALUE(m_pKD,"KD","Derivative gain");

	STATE_VARIABLE_REF(m_errorVariableIndex, "Input-Variable","The input state variable");

	END_CLASS();

	m_intError= 0.0;
}

CPIDController::~CPIDController()
{
	delete m_pKP;
	delete m_pKI;
	delete m_pKD;
}

int CPIDController::getNumOutputs()
{
	return 1;
}
int CPIDController::getOutputActionIndex(int output)
{
	if (output == 0)
		return m_outputActionIndex;
	throw std::exception("CLQRController. Invalid action output given.");
	return -1;
}

void CPIDController::selectAction(const CState *s, CAction *a)
{
	if (CApp::get()->pWorld->getT()== 0.0)
		m_intError= 0.0;

	double error= s->getValue(m_errorVariableIndex);
	double dError = error*CApp::get()->pWorld->getDT();
	m_intError += error*CApp::get()->pWorld->getDT();

	a->setValue(m_outputActionIndex
		,error*m_pKP->getValue() + m_intError*m_pKI->getValue() + dError*m_pKD->getValue());

}

double sgn(double value)
{
	if (value<0.0) return -1.0;
	else if (value>0.0) return 1.0;

	return 0.0;
}

//VIDAL////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

CWindTurbineVidalController::~CWindTurbineVidalController()
{
	delete m_pA;
	delete m_pK_alpha;
	delete m_pKP;
	delete m_pKI;
	delete m_P_s;
}

CLASS_CONSTRUCTOR(CWindTurbineVidalController)
{
	NUMERIC_VALUE(m_pA, "A", "A parameter of the torque controller");
	NUMERIC_VALUE(m_pK_alpha, "K_alpha", "K_alpha parameter of the torque controller");
	NUMERIC_VALUE(m_pKP, "KP", "Proportional gain of the pitch controller");
	NUMERIC_VALUE(m_pKI, "KI", "Integral gain of the pitch controller");
	NUMERIC_VALUE(m_P_s, "P_s", "Power setpoint for the torque controller");

	CState* pStateDescriptor = CWorld::getDynamicModel()->getStateDescriptor();
	m_omega_r_index = pStateDescriptor->getVarIndex("omega_r");
	m_d_omega_r_index = pStateDescriptor->getVarIndex("d_omega_r");
	m_E_p_index = pStateDescriptor->getVarIndex("E_p");
	m_T_g_index = pStateDescriptor->getVarIndex("T_g");
	m_beta_index = pStateDescriptor->getVarIndex("beta");
	m_E_int_omega_r_index = pStateDescriptor->getVarIndex("E_int_omega_r");

	CAction* pActionDescriptor = CWorld::getDynamicModel()->getActionDescriptor();

	m_d_beta_index = pActionDescriptor->getVarIndex("d_beta");
	m_d_T_g_index = pActionDescriptor->getVarIndex("d_T_g");
	END_CLASS();
}

int CWindTurbineVidalController::getNumOutputs()
{
	return 2;
}
int CWindTurbineVidalController::getOutputActionIndex(int output)
{
	switch (output)
	{
	case 0: return m_d_beta_index;
	case 1: return m_d_T_g_index;
	default: throw std::exception("CLQRController. Invalid action output given.");
	}
	
	return -1;
}

void CWindTurbineVidalController::selectAction(const CState *s,CAction *a)
{
	//f(omega_r,T_g,d_omega_r,E_p, E_int_omega_r)

	//d(Tg)/dt= (-1/omega_r)*(T_g*(a*omega_r-d_omega_r)-a*P_setpoint + K_alpha*sgn(P_a-P_setpoint))
	//d(beta)/dt= K_p*(omega_ref - omega_r) + K_i*(error_integral)

	double omega_r= s->getValue(m_omega_r_index);
	double d_omega_r= s->getValue(m_d_omega_r_index);
	double error_P= s->getValue(m_E_p_index);

	double T_g= s->getValue(m_T_g_index);
	double beta= s->getValue(m_beta_index);
	
	double d_T_g;
	
	if (omega_r!=0.0) d_T_g= (-1/omega_r)*(T_g*( m_pA->getValue() *omega_r+d_omega_r) 
		- m_pA->getValue()*m_P_s->getValue() + m_pK_alpha->getValue()*sgn(error_P));
	else d_T_g= 0.0;

	double e_omega_r = omega_r - 4.39823; //NOMINAL WIND SPEED
	double d_beta = m_pKP->getValue()*e_omega_r 
		+m_pKI->getValue()*s->getValue(m_E_int_omega_r_index);
				 /*0.5*K_p*error_omega*(1.0+sgn(error_omega))
				+ K_i*s->getValue("integrative_omega_r_error);*/

	a->setValue(m_d_beta_index,d_beta);
	a->setValue(m_d_T_g_index,d_T_g);

}

//BOUKHEZZAR CONTROLLER////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

CWindTurbineBoukhezzarController::~CWindTurbineBoukhezzarController()
{
	delete m_pC_0;
	delete m_pKP;
	delete m_pKI;
}

CLASS_CONSTRUCTOR(CWindTurbineBoukhezzarController)
{

	NUMERIC_VALUE(m_pC_0,"C_0", "C_0 parameter");
	NUMERIC_VALUE(m_pKP,"KP","Proportional gain of the pitch controller");
	NUMERIC_VALUE(m_pKI,"KI", "Integral gain of the pitch controller");
	CONST_DOUBLE_VALUE(m_J_t,"J_t",0.0,"Wind turbine model's J_t parameter (kg*m^2)");
	CONST_DOUBLE_VALUE(m_K_t,"K_t",0.0,"Wind turbine model's K_t parameter");

	CState* pStateDescriptor = CWorld::getDynamicModel()->getStateDescriptor();
	m_omega_r_index = pStateDescriptor->getVarIndex("omega_r");
	m_d_omega_r_index = pStateDescriptor->getVarIndex("d_omega_r");
	m_E_p_index = pStateDescriptor->getVarIndex("E_p");
	m_T_g_index = pStateDescriptor->getVarIndex("T_g");
	m_T_a_index = pStateDescriptor->getVarIndex("T_a");
	m_beta_index = pStateDescriptor->getVarIndex("beta");

	CAction* pActionDescriptor = CWorld::getDynamicModel()->getActionDescriptor();

	m_d_beta_index = pActionDescriptor->getVarIndex("d_beta");
	m_d_T_g_index = pActionDescriptor->getVarIndex("d_T_g");
	END_CLASS();
}

int CWindTurbineBoukhezzarController::getNumOutputs()
{
	return 2;
}
int CWindTurbineBoukhezzarController::getOutputActionIndex(int output)
{
	switch (output)
	{
	case 0: return m_d_beta_index;
	case 1: return m_d_T_g_index;
	default: throw std::exception("CLQRController. Invalid action output given.");
	}

	return -1;
}


void CWindTurbineBoukhezzarController::selectAction(const CState *s,CAction *a)
{
	//d(Tg)/dt= (1/omega_r)*(C_0*error_P - (1/J_t)*(T_a*T_g - K_t*omega_r*T_g - T_g*T_g))
	//d(beta)/dt= K_p*(omega_ref - omega_r)

	double omega_r= s->getValue(m_omega_r_index);	// state->getContinuousState(DIM_omega_r);
	double C_0= m_pC_0->getValue();					//getParameter("C0");
	double error_P= -s->getValue(m_E_p_index);		//-state->getContinuousState(DIM_P_error);
	double T_a= s->getValue(m_T_a_index);			//state->getContinuousState(DIM_T_a);

	double T_g= s->getValue(m_T_g_index);			//state->getContinuousState(DIM_T_g);
	double beta= s->getValue(m_beta_index);		//state->getContinuousState(DIM_beta);
	
	double d_T_g= (1.0/omega_r)*(C_0*error_P - (1.0/m_J_t)
		*(T_a*T_g - m_K_t*omega_r*T_g - T_g*T_g));

	double e_omega_r = omega_r - 4.39823; //NOMINAL WIND SPEED
	double d_beta = m_pKP->getValue()*e_omega_r + m_pKI->getValue()*s->getValue(m_E_int_omega_r_index);

	a->setValue(m_d_beta_index,d_beta); //action->setActionValue(DIM_A_beta,d_beta);
	a->setValue(m_d_T_g_index,d_T_g); //action->setActionValue(DIM_A_torque,d_T_g);

}

//JONKMAN//////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

CWindTurbineJonkmanController::~CWindTurbineJonkmanController()
{
	delete m_PC_KK;
	delete m_PC_KP;
	delete m_PC_KI;
}

CLASS_CONSTRUCTOR(CWindTurbineJonkmanController)
{
	//GENERATOR SPEED FILTER PARAMETERS
	CONST_DOUBLE_VALUE(m_CornerFreq,"CornerFreq",0.0,"Corner Freq. parameter");

	//TORQUE CONTROLLER'S PARAMETERS
	CONST_DOUBLE_VALUE(m_VS_RtGnSp,"VSRtGnSp",0.0,"Rated Generator Speed");
	CONST_DOUBLE_VALUE(m_VS_SlPc,"VS_SlPc",0.0,"SIPc parameter");
	CONST_DOUBLE_VALUE(m_VS_Rgn2K,"VS_Rgn2K",0.0,"Rgn2K parameter");
	CONST_DOUBLE_VALUE(m_VS_Rgn2Sp,"VS_Rgn2Sp",0.0,"Rgn2Sp parameter");
	CONST_DOUBLE_VALUE(m_VS_CtInSp,"VS_CtInSp",0.0,"CtlnSp parameter");
	CONST_DOUBLE_VALUE(m_VS_RtPwr,"VS_RtPwr",0.0,"Rated power");
	CONST_DOUBLE_VALUE(m_VS_Rgn3MP,"VS_Rgn3MP",0.0,"Rgn3MP parameter");
	
	m_VS_SySp    = m_VS_RtGnSp/( 1.0 +  0.01*m_VS_SlPc );
	m_VS_Slope15 = ( m_VS_Rgn2K*m_VS_Rgn2Sp*m_VS_Rgn2Sp )/( m_VS_Rgn2Sp - m_VS_CtInSp );
	m_VS_Slope25 = ( m_VS_RtPwr/m_VS_RtGnSp           )/( m_VS_RtGnSp - m_VS_SySp   );

	if ( m_VS_Rgn2K == 0.0 )  //.TRUE. if the Region 2 torque is flat, and thus, the denominator in the ELSE condition is zero
		m_VS_TrGnSp = m_VS_SySp;
	else                          //.TRUE. if the Region 2 torque is quadratic with speed
		m_VS_TrGnSp = ( m_VS_Slope25 - sqrt( m_VS_Slope25*( m_VS_Slope25 - 4.0*m_VS_Rgn2K*m_VS_SySp ) ) )/( 2.0*m_VS_Rgn2K );

	//PITCH CONTROLLER'S PARAMETERS
	NUMERIC_VALUE(m_PC_KK,"PC_KK","PC_KK");
	NUMERIC_VALUE(m_PC_KP,"PC_KP","PC_KP");
	NUMERIC_VALUE(m_PC_KI,"PC_KI","PC_KI");
	CONST_DOUBLE_VALUE(m_PC_RefSpd,"PC_RefSpd",0.0,"Pitch control reference speed");

	m_IntSpdErr= 0.0;

	CState* pStateDescriptor = CWorld::getDynamicModel()->getStateDescriptor();
	m_omega_g_index = pStateDescriptor->getVarIndex("omega_g");

	m_E_p_index = pStateDescriptor->getVarIndex("E_p");
	m_T_g_index = pStateDescriptor->getVarIndex("T_g");
	m_beta_index = pStateDescriptor->getVarIndex("beta");

	CAction* pActionDescriptor = CWorld::getDynamicModel()->getActionDescriptor();

	m_d_beta_index = pActionDescriptor->getVarIndex("d_beta");
	m_d_T_g_index = pActionDescriptor->getVarIndex("d_T_g");
	END_CLASS();
}

int CWindTurbineJonkmanController::getNumOutputs()
{
	return 2;
}
int CWindTurbineJonkmanController::getOutputActionIndex(int output)
{
	switch (output)
	{
	case 0: return m_d_beta_index;
	case 1: return m_d_T_g_index;
	default: throw std::exception("CLQRController. Invalid action output given.");
	}

	return -1;
}

void CWindTurbineJonkmanController::selectAction(const CState *s,CAction *a)
{
	//Filter the generator speed
	double Alpha;

	if (CApp::get()->pWorld->getT() == 0.0)
	{
		Alpha= 1.0;
		m_GenSpeedF= s->getValue(m_omega_g_index);
	}
	else Alpha = exp((CApp::get()->pWorld->getDT())*m_CornerFreq);
	m_GenSpeedF = ( 1.0 - Alpha )*s->getValue(m_omega_g_index) + Alpha*m_GenSpeedF;

	//TORQUE CONTROLLER
	double GenTrq;
	if ( (   m_GenSpeedF >= m_VS_RtGnSp ) || (  s->getValue(m_beta_index) >= m_VS_Rgn3MP ) )   //We are in region 3 - power is constant
		GenTrq = m_VS_RtPwr/m_GenSpeedF;
	else if ( m_GenSpeedF <= m_VS_CtInSp )                                      //We are in region 1 - torque is zero
		GenTrq = 0.0;
	else if ( m_GenSpeedF <  m_VS_Rgn2Sp )                                      //We are in region 1 1/2 - linear ramp in torque from zero to optimal
		GenTrq = m_VS_Slope15*( m_GenSpeedF - m_VS_CtInSp );
	else if ( m_GenSpeedF <  m_VS_TrGnSp )                                      //We are in region 2 - optimal torque is proportional to the square of the generator speed
		GenTrq = m_VS_Rgn2K*m_GenSpeedF*m_GenSpeedF;
	else                                                                       //We are in region 2 1/2 - simple induction generator transition region
		GenTrq = m_VS_Slope25*( m_GenSpeedF - m_VS_SySp   );

	GenTrq  = std::min( GenTrq, s->getMax("T_g")  );   //Saturate the command using the maximum torque limit

	double TrqRate;
	TrqRate = (GenTrq - s->getValue(m_T_g_index)) / CApp::get()->pWorld->getDT(); //Torque rate (unsaturated)
	a->setValue(m_d_T_g_index,TrqRate);

	//PITCH CONTROLLER
	double GK = 1.0/( 1.0 + s->getValue(m_beta_index)/m_PC_KK->getValue() );

	//Compute the current speed error and its integral w.r.t. time; saturate the
	//  integral term using the pitch angle limits:
	double SpdErr    = m_GenSpeedF - m_PC_RefSpd;                                 //Current speed error
	m_IntSpdErr = m_IntSpdErr + SpdErr*CApp::get()->pWorld->getDT();                           //Current integral of speed error w.r.t. time
	//Saturate the integral term using the pitch angle limits, converted to integral speed error limits
	m_IntSpdErr = std::min( std::max( m_IntSpdErr, s->getMax(m_beta_index)/( GK*m_PC_KI->getValue() ) )
		, s->getMin("beta")/( GK*m_PC_KI->getValue() ));
  
	//Compute the pitch commands associated with the proportional and integral
	//  gains:
	double PitComP   = GK* m_PC_KP->getValue() *   SpdErr; //Proportional term
	double PitComI   = GK* m_PC_KI->getValue() * m_IntSpdErr; //Integral term (saturated)

	//Superimpose the individual commands to get the total pitch command;
	//  saturate the overall command using the pitch angle limits:
	double PitComT   = PitComP + PitComI;                                     //Overall command (unsaturated)
	PitComT   = std::min( std::max( PitComT, s->getMin(m_beta_index) ), s->getMax(m_beta_index) );           //Saturate the overall command using the pitch angle limits

	//Saturate the overall commanded pitch using the pitch rate limit:
	//NOTE: Since the current pitch angle may be different for each blade
	//      (depending on the type of actuator implemented in the structural
	//      dynamics model), this pitch rate limit calculation and the
	//      resulting overall pitch angle command may be different for each
	//      blade.

	double d_beta = (PitComT - s->getValue(m_beta_index)) / CApp::get()->pWorld->getDT();
	
	a->setValue(m_d_beta_index,d_beta);
	/*
	for (int k=1; k<=NumBl; k++) //Loop through all blades
	{
		PitRate[k -1] = ( PitComT - BlPitch[k -1] )/ElapTime; //Pitch rate of blade K (unsaturated)
		PitRate[k -1] = min( max( PitRate[k -1], -PC_MaxRat ), PC_MaxRat ); //Saturate the pitch rate of blade K using its maximum absolute value
		PitCom [k -1] = BlPitch[k -1] + PitRate[k -1]*ElapTime; //Saturate the overall command of blade K using the pitch rate limit

		PitCom[k -1]  = min( max( PitCom[k -1], PC_MinPit ), PC_MaxPit ); //Saturate the overall command using the pitch angle limits         
	}  */
}