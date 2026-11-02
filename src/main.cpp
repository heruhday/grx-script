#include <Windows.h>
#include "arxEntryPoint.h"
#include "acutads.h"

class Arx01App : public AcRxArxApp
{
public:
  virtual void RegisterServerComponents() override
  {
  }

  virtual AcRx::AppRetCode On_kInitAppMsg(void* pkt) override
  {
    const AcRx::AppRetCode result = AcRxArxApp::On_kInitAppMsg(pkt);
    acutPrintf(_RXST("\narx01 ARX loaded. Type HELLO to run the command."));
    return result;
  }

  virtual AcRx::AppRetCode On_kUnloadAppMsg(void* pkt) override
  {
    acutPrintf(_RXST("\narx01 ARX unloaded."));
    return AcRxArxApp::On_kUnloadAppMsg(pkt);
  }

  static void ARX01Hello()
  {
    acutPrintf(_RXST("\narx01 HELLO command executed."));
  }
};

ACED_ARXCOMMAND_ENTRY_AUTO(Arx01App, ARX01, Hello, HELLO, ACRX_CMD_MODAL, NULL)
IMPLEMENT_ARX_ENTRYPOINT(Arx01App)
