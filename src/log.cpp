#include <Arduino.h>

#include "log.h"
#include "config.h"

LogConsoleType logConsole;

void logPush(char c)
{
  logConsole.push(c);
}

String logPrint()
{

  String buff = "";

  if (logConsole.isEmpty()) {
    return "";
  } else {
    for (decltype(logConsole)::index_t i = 0; i < logConsole.size() - 1; i++) {
      buff += logConsole[i];
    }
    return buff;
  }
}

void logClear()
{
  Serial.println("\n========== LOG CLEAR ==========");
  Serial.print("[LOG] Current log buffer size: ");
  Serial.println(logConsole.size());
  
  if (!logConsole.isEmpty()) {
    logConsole.clear();
    Serial.println("[LOG] Log buffer cleared successfully");
  } else {
    Serial.println("[LOG] Log buffer already empty");
  }
  Serial.println("================================\n");
}
