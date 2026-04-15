void clearS2Buffer();
void getZbVer();
void zbCheck();
void zbLedToggle();
int8_t checkFwHex(const char *tempFile);
int8_t runFlash(const char *hexFilePath,uint32_t binsize);
bool programFlashFromFile(const char *filePath);
void zbInit();
void zbBegin(void);



bool zbSendCommand(uint8_t cmd, uint8_t *data, uint8_t len ,uint8_t timeout);
void zbSendAck(void);
bool zbGetStatus(uint8_t *status,uint32_t timeout);
bool zbCheckLastCmd(void);
bool zbFlashStart(uint32_t addr,uint32_t flashSize);
bool zbEraseFlash(uint32_t start,uint32_t size);
size_t getHexBinSize(File hexFile);
bool zbSendSync(uint32_t timeout);
