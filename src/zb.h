void clearS2Buffer();
void getZbVer();
void zbCheck();
void zbLedToggle();
void checkFwHex(const char *tempFile);
void runFlash(const char *tempFile);
bool programFlashFromFile(const char *filePath);
void zbInit();
void zbBegin(void);



bool zbSendCommand(uint8_t cmd, uint8_t *data, uint8_t len ,uint8_t timeout);
void zbSendAck(void);
bool zbGetStatus(uint8_t *status,uint32_t timeout);
bool zbCheckLastCmd(void);
 bool zbFlashStart(const char *hexFilePath, uint32_t addr,uint32_t *flashSize);
bool zbEraseFlash(uint32_t start,uint32_t size);
size_t getHexBinSize(const char *hexFilePath);
bool zbSendSync(uint32_t timeout);
