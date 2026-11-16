#pragma once
// The balance settings as the server's files give them: [balance] of server.ini and balance_exclude.ini beside it
// (docs/FORK.md). Sent to every player in S_BALANCE.
#include <string>

#include "common/api/protocol/Balance.h"

namespace coop::server {

// What the files say now. A key server.ini does not set has its preset (Balance.h), a missing file changes nothing.
// summary: one line for the log, the values that differ from the game's own.
proto::Balance LoadBalance(const std::wstring& serverIni, std::string& summary);

// Adds what is missing for the owner to edit: the [balance] section in server.ini (with every key at its preset) and
// balance_exclude.ini beside it.
void WriteBalanceDefaults(const std::wstring& serverIni);

std::wstring BalanceExcludePath(const std::wstring& serverIni);

}  // namespace coop::server
