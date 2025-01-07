/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _HAWKBIT_H_
#define _HAWKBIT_H_

/* Public types --------------------------------------------------------------*/

enum hawkbit_security_type
{
  HAWKBIT_SEC_CLIENTCERTIFICATE,
  HAWKBIT_SEC_GATEWAYTOKEN,
  HAWKBIT_SEC_TARGETTOKEN,
  HAWKBIT_SEC_NONE,
  HAWKBIT_SEC_MAX
};

enum hawkbit_execution_status
{
  HAWKBIT_EX_CANCELED,
  HAWKBIT_EX_REJECTED,
  HAWKBIT_EX_CLOSED,
  HAWKBIT_EX_PROCEEDING,
  HAWKBIT_EX_SCHEDULED,
  HAWKBIT_EX_RESUMED,
  HAWKBIT_EX_MAX
};

enum hawkbit_execution_result
{
  HAWKBIT_RES_NONE,
  HAWKBIT_RES_SUCCESS,
  HAWKBIT_RES_FAILURE,
  HAWKBIT_RES_MAX
};

enum hawkbit_config_data_mode
{
  HAWKBIT_CONFIGDATA_MERGE,
  HAWKBIT_CONFIGDATA_REPLACE,
  HAWKBIT_CONFIGDATA_REMOVE,
  HAWKBIT_CONFIGDATA_MAX
};

enum hawkbit_deployment_mode
{
  HAWKBIT_DEPLOYMENT_NONE,
  HAWKBIT_DEPLOYMENT_SKIP,
  HAWKBIT_DEPLOYMENT_ATTEMPT,
  HAWKBIT_DEPLOYMENT_FORCE,
  HAWKBIT_DEPLOYMENT_MAX
};

/* De-/Constructors */
hawkbit_Init( void );
hawkbit_Deinit( void );

#endif /* ___HAWKBIT_DDI_H___ */