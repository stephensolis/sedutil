/* C:B**************************************************************************
This software is Copyright 2014,2015 Michael Romeo <r0m30@r0m30.com>

This file is part of msed.

msed is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

msed is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with msed.  If not, see <http://www.gnu.org/licenses/>.

 * C:E********************************************************************** */
/** Device class for Opal 2.0 SSC
 * also supports the Opal 1.0 SSC
 */
#include "os.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include<iomanip>
#include "MsedDevEnterprise.h"
#include "MsedHashPwd.h"
#include "MsedEndianFixup.h"
#include "MsedStructures.h"
#include "MsedCommand.h"
#include "MsedResponse.h"
#include "MsedSession.h"
#include "MsedHexDump.h"

using namespace std;


MsedDevEnterprise::MsedDevEnterprise(const char * devref)
{
	MsedDevOS::init(devref);
	if (properties())
		LOG(E) << "Properties exchange failed";
}

MsedDevEnterprise::~MsedDevEnterprise()
{
}

uint8_t MsedDevEnterprise::initialsetup(char * password)
{
	LOG(D1) << "Entering initialSetup()";
	if (takeOwnership(password)) {
		LOG(E) << "Initial setup failed - unable to take ownership";
		return 0xff;
	}
	if (activateLockingSP(password)) {
		LOG(E) << "Initial setup failed - unable to activate LockingSP";
		return 0xff;
	}
	if (setReadLocked(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
		OPAL_TOKEN::OPAL_FALSE, password)) {
		LOG(E) << "Initial setup failed - unable to unlock for read";
		return 0xff;
	}
	if (setWriteLocked(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
		OPAL_TOKEN::OPAL_FALSE, password)) {
		LOG(E) << "Initial setup failed - unable to unlock for write";
		return 0xff;
	}
	if (setReadLockEnabled(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
		OPAL_TOKEN::OPAL_TRUE, password)) {
		LOG(E) << "Initial setup failed - unable to enable readlocking";
		return 0xff;
	}
	if (setWriteLockEnabled(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
		OPAL_TOKEN::OPAL_TRUE, password)) {
		LOG(E) << "Initial setup failed - unable to enable writelocking";
		return 0xff;
	}
	LOG(I) << "Initial setup of TPer complete on " << dev;
	LOG(D1) << "Exiting initialSetup()";
	return 0;
}
uint8_t MsedDevEnterprise::setReadLocked(OPAL_UID lockingrange, OPAL_TOKEN state,
	char * password){
	return setLockingSPvalue(lockingrange, OPAL_TOKEN::READLOCKED, state, password, NULL);
}
uint8_t MsedDevEnterprise::setWriteLocked(OPAL_UID lockingrange, OPAL_TOKEN state,
	char * password) {
	return setLockingSPvalue(lockingrange, OPAL_TOKEN::WRITELOCKED, state, password, NULL);
}
uint8_t MsedDevEnterprise::setReadLockEnabled(OPAL_UID lockingrange, OPAL_TOKEN state,
	char * password) {
	return setLockingSPvalue(lockingrange, OPAL_TOKEN::READLOCKENABLED, state, password, NULL);
}
uint8_t MsedDevEnterprise::setWriteLockEnabled(OPAL_UID lockingrange, OPAL_TOKEN state,
	char * password) {
	return setLockingSPvalue(lockingrange, OPAL_TOKEN::WRITELOCKENABLED, state, password, NULL);
}
uint8_t MsedDevEnterprise::configureLockingRange(uint8_t lockingrange, OPAL_TOKEN enabled, char * password)
{
	LOG(D1) << "Entering MsedDevEnterprise::configureLockingRange()";
	if (lockingrange) {
		LOG(E) << "Only global locking range is currently supported";
		return 0xff;
	}
	if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
		OPAL_TOKEN::READLOCKENABLED, enabled, password, NULL)) {
		LOG(E) << "Configure Locking range failed - unable to set readlockenabled";
		return 0xff;
	}
	if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
		OPAL_TOKEN::WRITELOCKENABLED, enabled, password, NULL)) {
		LOG(E) << "Configure Locking range failed - unable to set writelockenabled";
		return 0xff;
	}
	LOG(I) << "Locking range configured " << (uint16_t) enabled;
	LOG(D1) << "Exiting MsedDevEnterprise::configureLockingRange()";
	return 0;
}
uint8_t MsedDevEnterprise::revertLockingSP(char * password, uint8_t keep)
{
	LOG(D1) << "Entering revert MsedDevEnterprise::LockingSP() keep = " << keep;
	vector<uint8_t> keepgloballockingrange;
	keepgloballockingrange.push_back(0xa3);
	keepgloballockingrange.push_back(0x06);
	keepgloballockingrange.push_back(0x00);
	keepgloballockingrange.push_back(0x00);
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	MsedCommand *cmd = new MsedCommand();
	session = new MsedSession(this);
	// session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
	//                   HostChallenge = <Admin1_password>, HostSigningAuthority = Admin1_UID]

	if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
		delete cmd;
		delete session;
		return 0xff;
	}
	// session[TSN:HSN]->ThisSP.RevertSP[]
	cmd->reset(OPAL_UID::OPAL_THISSP_UID, OPAL_METHOD::REVERTSP);
	cmd->addToken(OPAL_TOKEN::STARTLIST);
	if (keep) {
		cmd->addToken(OPAL_TOKEN::STARTNAME);
		cmd->addToken(keepgloballockingrange);
		cmd->addToken(OPAL_TINY_ATOM::UINT_01); // KeepGlobalRangeKey = TRUE
		cmd->addToken(OPAL_TOKEN::ENDNAME);
	}
	cmd->addToken(OPAL_TOKEN::ENDLIST);
	cmd->complete();
	if (session->sendCommand(cmd, response)) {
		delete cmd;
		delete session;
		return 0xff;
	}
	// empty list returned so rely on method status
	LOG(I) << "Revert LockingSP complete";
	session->expectAbort();
	delete session;
	LOG(D1) << "Exiting revert MsedDev:LockingSP()";
	return 0;
}
uint8_t MsedDevEnterprise::getAuth4User(char * userid, uint8_t uidorcpin, std::vector<uint8_t> &userData)
{
	LOG(D1) << "Entering MsedDevEnterprise::getAuth4User()";
	uint8_t uidnum;
	userData.clear();
	userData.push_back(0xa8);
	userData.push_back(0x00);
	userData.push_back(0x00);
	userData.push_back(0x00);
	switch (uidorcpin) {
	case 0:
		userData.push_back(0x09);
		break;
	case 10:
		userData.push_back(0x0b);
		break;
	default:
		LOG(E) << "Invalid Userid data requested" << (uint16_t)uidorcpin;
		return 0xff;
	}


	switch (strnlen(userid, 20)) {
	case 5:
		if (memcmp("User", userid, 4)) {
			LOG(E) << "Invalid Userid " << userid;
			return 0xff;
		}
		else {
			uidnum = userid[4] & 0x0f;
			userData.push_back(0x00);
			userData.push_back(0x03);
			userData.push_back(0x00);
			userData.push_back(uidnum);
		}
		break;
	case 6:
		if (memcmp("Admin", userid, 5)) {
			LOG(E) << "Invalid Userid " << userid;
			return 0xff;
		}
		else {
			uidnum = userid[5] & 0x0f;
			userData.push_back(0x00);
			userData.push_back(0x01);
			userData.push_back(0x00);
			userData.push_back(uidnum);
		}
		break;
	default:
		LOG(E) << "Invalid Userid " << userid;
		return 0xff;
	}
	if (0 == uidnum) {
		LOG(E) << "Invalid Userid " << userid;
		userData.clear();
		return 0xff;
	}
	LOG(D1) << "Exiting MsedDevEnterprise::getAuth4User()";
	return 0;
}
// Samsung EVO 840 will not return userids from authority table (bug??)
//int getAuth4User(char * userid, uint8_t column, std::vector<uint8_t> &userData)
//{
//    LOG(D1) << "Entering getAuth4User()";
//    std::vector<uint8_t> table, key, nextkey;
//    // build a token for the authority table
//    table.push_back(0xa8);
//    for (int i = 0; i < 8; i++) {
//        table.push_back(OPALUID[OPAL_UID::OPAL_AUTHORITY_TABLE][i]);
//    }
//    key.clear();
//    while (true) {
//        // Get the next UID
//        if (nextTable(session, table, key, response)) {
//            return 0xff;
//        }
//        key = response.getRawToken(2);
//        nextkey = response.getRawToken(3);
//
//        //AUTHORITY_TABLE.Get[Cellblock : [startColumn = 0, endColumn = 10]]
//        if (getTable(session, key, 1, 1, response)) {
//            return 0xff;
//        }
//        if (!(strcmp(userid, response.getString(4).c_str()))) {
//            if (getTable(session, key, column, column, response)) {
//                return 0xff;
//            }
//            userData = response.getRawToken(4);
//            return 0x00;
//        }
//
//        if (9 != nextkey.size()) break; // no next row so bail
//        key = nextkey;
//    }
//    return 0xff;
//}
uint8_t MsedDevEnterprise::setNewPassword(char * password, char * userid, char * newpassword)
{
	LOG(D1) << "Entering MsedDevEnterprise::setNewPassword" ;
	std::vector<uint8_t> userCPIN, hash;
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	session = new MsedSession(this);
	// session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
	//               HostChallenge = <new_SID_password>, HostSigningAuthority = Admin1_UID]
	if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
		delete session;
		return 0xff;
	}
	// Get C_PIN ID  for user from Authority table
	if (getAuth4User(userid, 10, userCPIN)) {
		LOG(E) << "Unable to find user " << userid << " in Authority Table";
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] -> C_PIN_user_UID.Set[Values = [PIN = <new_password>]]
	MsedHashPwd(hash, newpassword, this);
	if (setTable(userCPIN, OPAL_TOKEN::PIN, hash)) {
		LOG(E) << "Unable to set user " << userid << " new password ";
		delete session;
		return 0xff;
	}
	LOG(I) << userid << " password changed";
	// session[TSN:HSN] <- EOS
	delete session;
	LOG(D1) << "Exiting MsedDevEnterprise::setNewPassword()";
	return 0;
}
uint8_t MsedDevEnterprise::setMBREnable(uint8_t mbrstate,	char * Admin1Password)
{
	LOG(D1) << "Entering MsedDevEnterprise::setMBREnable";

	if (mbrstate) {
		if (setLockingSPvalue(OPAL_UID::OPAL_MBRCONTROL, OPAL_TOKEN::MBRENABLE,
			OPAL_TOKEN::OPAL_TRUE, Admin1Password, NULL)) {
			LOG(E) << "Unable to set setMBREnable on";
			return 0xff;
		}
	} else {
		if (setLockingSPvalue(OPAL_UID::OPAL_MBRCONTROL, OPAL_TOKEN::MBRENABLE,
				OPAL_TOKEN::OPAL_FALSE, Admin1Password, NULL)) {
				LOG(E) << "Unable to set setMBREnable off";
				return 0xff;
			}
	}
	LOG(D1) << "Exiting MsedDevEnterprise::setMBREnable";
	return 0;
}
uint8_t MsedDevEnterprise::setMBRDone(uint8_t mbrstate, char * Admin1Password)
{
	LOG(D1) << "Entering MsedDevEnterprise::setMBRDone";

	if (mbrstate) {
		if (setLockingSPvalue(OPAL_UID::OPAL_MBRCONTROL, OPAL_TOKEN::MBRDONE,
			OPAL_TOKEN::OPAL_TRUE, Admin1Password, NULL)) {
			LOG(E) << "Unable to set setMBRDone on";
			return 0xff;
		}
	}
	else {
		if (setLockingSPvalue(OPAL_UID::OPAL_MBRCONTROL, OPAL_TOKEN::MBRDONE,
			OPAL_TOKEN::OPAL_FALSE, Admin1Password, NULL)) {
			LOG(E) << "Unable to set setMBRDone off";
			return 0xff;
		}
	}
	LOG(D1) << "Exiting MsedDevEnterprise::setMBRDone";
	return 0;
}
uint8_t MsedDevEnterprise::setLockingRange(uint8_t lockingrange, uint8_t lockingstate,
	char * Admin1Password)
{
	LOG(D1) << "Entering MsedDevEnterprise::setLockingRange";
	
	if (lockingrange) {
		LOG(E) << "Only global locking range is currently supported";
		return 0xff;
	}
	switch (lockingstate) {
	case 0x01:
		if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL, OPAL_TOKEN::READLOCKED,
			OPAL_TOKEN::OPAL_FALSE, Admin1Password, NULL)) {
			LOG(E) << "Set Lockingstate failed - unable to unlock for read";
			return 0xff;
		}
		if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL, OPAL_TOKEN::WRITELOCKED,
			OPAL_TOKEN::OPAL_FALSE, Admin1Password, NULL)) {
			LOG(E) << "Set Lockingstate failed - unable to unlock for write ";
			return 0xff;
		}
		LOG(I) << "LockingRange" << (uint16_t)lockingrange << " set to RW";
		return 0;
	case 0x02:
		if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL, OPAL_TOKEN::READLOCKED,
			OPAL_TOKEN::OPAL_FALSE, Admin1Password, NULL)) {
			LOG(E) << "Set Lockingstate failed - unable to unlock for read";
			return 0xff;
		}
		if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL, OPAL_TOKEN::WRITELOCKED,
			OPAL_TOKEN::OPAL_TRUE, Admin1Password, NULL)) {
			LOG(E) << "Set Lockingstate failed - unable to lock for write";
			return 0xff;
		}
		LOG(I) << "LockingRange" << (uint16_t)lockingrange << " set to RO";
		return 0;
	case 0x03:
		if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL, OPAL_TOKEN::READLOCKED,
			OPAL_TOKEN::OPAL_TRUE, Admin1Password, NULL)) {
			LOG(E) << "Set Lockingstate failed - unable to lock for read";
			return 0xff;
		}
		if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL, OPAL_TOKEN::WRITELOCKED,
			OPAL_TOKEN::OPAL_TRUE, Admin1Password, NULL)) {
			LOG(E) << "Set Lockingstate failed - unable to lock for write";
			return 0xff;
		}
		LOG(I) << "LockingRange" << (uint16_t)lockingrange << " set to LK";
		return 0;
	default:
		LOG(E) << "Invalid locking state for setLockingRange";
		return 0xff;
	}
}
uint8_t MsedDevEnterprise::setLockingSPvalue(OPAL_UID table_uid, OPAL_TOKEN name, 
	OPAL_TOKEN value,char * password, char * msg)
{
	LOG(D1) << "Entering MsedDevEnterprise::setLockingSPvalue";
	vector<uint8_t> table;
	table.push_back(0xa8);
	for (int i = 0; i < 8; i++) {
		table.push_back(OPALUID[table_uid][i]);
	}
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal compliant " << dev;
		return 0xff;
	}
	session = new MsedSession(this);
	// session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
	//               HostChallenge = <password>, HostSigningAuthority = Admin1_UID]
	if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
		delete session;
		return 0xff;
	}
	if (setTable(table, name, value)) {
		LOG(E) << "Unable to update table";
		delete session;
		return 0xff;
	}
	//getTable(session, table, 1, 10, response);
	if (NULL != msg) {
		LOG(I) << msg;
	}
	// session[TSN:HSN] <- EOS
	delete session;
	LOG(D1) << "Exiting MsedDevEnterprise::setLockingSPvalue()";
	return 0;
}

uint8_t MsedDevEnterprise::enableUser(char * password, char * userid)
{
	LOG(D1) << "Entering MsedDevEnterprise::enableUser";
	vector<uint8_t> userUID;
	
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	session = new MsedSession(this);
	// session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
	//               HostChallenge = <new_SID_password>, HostSigningAuthority = Admin1_UID]
	if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
		delete session;
		return 0xff;
	}
	// Get UID for user from Authority table
	if (getAuth4User(userid, 0, userUID)) {
		LOG(E) << "Unable to find user " << userid << " in Authority Table";
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] -> User1_UID.Set[Values = [Enabled = TRUE]]
	if (setTable(userUID, (OPAL_TOKEN)0x05, OPAL_TOKEN::OPAL_TRUE)) {
		LOG(E) << "Unable to enable user " << userid;
		delete session;
		return 0xff;
	}
	LOG(I) << userid << " has been enabled ";
	// session[TSN:HSN] <- EOS
	delete session;
	LOG(D1) << "Exiting MsedDevEnterprise::enableUser()";
	return 0;
}
uint8_t MsedDevEnterprise::revertTPer(char * password, uint8_t PSID)
{
	LOG(D1) << "Entering MsedDevEnterprise::revertTPer()";
	if (!isSupportedSSC()) {
		LOG(E) << "Device not supported for PSID Revert " << dev;
		return 0xff;
	}
	MsedCommand *cmd = new MsedCommand();
	session = new MsedSession(this);
	OPAL_UID uid = OPAL_UID::OPAL_SID_UID;
	if (PSID) {
		session->dontHashPwd(); // PSID pwd should be passed as entered
		uid = OPAL_UID::OPAL_PSID_UID;
	}
	if (session->start(OPAL_UID::OPAL_ADMINSP_UID, password, uid)) {
		delete cmd;
		delete session;
		return 0xff;
	}
	//	session[TSN:HSN]->AdminSP_UID.Revert[]
	cmd->reset(OPAL_UID::OPAL_ADMINSP_UID, OPAL_METHOD::REVERT);
	cmd->addToken(OPAL_TOKEN::STARTLIST);
	cmd->addToken(OPAL_TOKEN::ENDLIST);
	cmd->complete();
	session->expectAbort();
	if (session->sendCommand(cmd, response)) {
		delete cmd;
		delete session;
		return 0xff;
	}
	LOG(I) << "revertTper completed successfully";
	delete cmd;
	delete session;
	LOG(D1) << "Exiting MsedDevEnterprise::RevertTperevertTPer()";
	return 0;
}
uint8_t MsedDevEnterprise::loadPBA(char * password, char * filename) {
	LOG(D1) << "Entering MsedDevEnterprise::loadPBAimage()" << filename << " " << dev;
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	uint64_t fivepercent = 0;
	int complete = 4;
	typedef struct { uint8_t  i : 2; } spinnertik;
	spinnertik spinnertick;
	spinnertick.i = 0;
	char star[] = "*";
	char spinner[] = "|/-\\";
	char progress_bar[] = "   [                     ]";

	uint32_t filepos = 0;
	ifstream pbafile;
	vector <uint8_t> buffer, lengthtoken;
	buffer.clear();
	buffer.reserve(1024);
	for (int i = 0; i < 1024; i++) {
		buffer.push_back(0x00);
	}
	lengthtoken.clear();
	lengthtoken.push_back(0xd4);
	lengthtoken.push_back(0x00);
	pbafile.open(filename, ios::in | ios::binary);
	if (!pbafile) {
		LOG(E) << "Unable to open PBA image file " << filename;
		return 0xff;
	}
	pbafile.seekg(0, pbafile.end);
	fivepercent = ((pbafile.tellg() / 20) / 1024) * 1024;
	if (0 == fivepercent) fivepercent++;
	pbafile.seekg(0, pbafile.beg);

	MsedCommand *cmd = new MsedCommand();
	session = new MsedSession(this);
	if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
		delete cmd;
		delete session;
		pbafile.close();
		return 0xff;
	}
	LOG(I) << "Writing PBA to " << dev;
	while (!pbafile.eof()) {
		pbafile.read((char *)buffer.data(), 1024);
		if (!(filepos % fivepercent)) progress_bar[complete++] = star[0];
		if (!(filepos % (1024 * 5))) {
			progress_bar[1] = spinner[spinnertick.i++];
			printf("\r%s", progress_bar);
			fflush(stdout);
		}
		//	session[TSN:HSN] -> MBR_UID.Set[Where = 0, Values = <Master_Boot_Record_shadow>]
		cmd->reset(OPAL_UID::OPAL_MBR, OPAL_METHOD::SET);
		cmd->addToken(OPAL_TOKEN::STARTLIST);
		cmd->addToken(OPAL_TOKEN::STARTNAME);
		cmd->addToken(OPAL_TOKEN::WHERE);
		cmd->addToken(filepos);
		cmd->addToken(OPAL_TOKEN::ENDNAME);
		cmd->addToken(OPAL_TOKEN::STARTNAME);
		cmd->addToken(OPAL_TOKEN::VALUES);
		cmd->addToken(lengthtoken);
		cmd->addToken(buffer);
		cmd->addToken(OPAL_TOKEN::ENDNAME);
		cmd->addToken(OPAL_TOKEN::ENDLIST);
		cmd->complete();
		if (session->sendCommand(cmd, response)) {
			delete cmd;
			delete session;
			pbafile.close();
			return 0xff;
		}
		filepos += 1024;
	}
	printf("\n");
	delete cmd;
	delete session;
	pbafile.close();
	LOG(I) << "PBA image  " << filename << " written to " << dev;
	LOG(D1) << "Exiting MsedDevEnterprise::loadPBAimage()";
	return 0;
}

uint8_t MsedDevEnterprise::activateLockingSP(char * password)
{
	LOG(D1) << "Entering MsedDevEnterprise::activateLockingSP()";
	vector<uint8_t> table;
	table.push_back(0xa8);
	for (int i = 0; i < 8; i++) {
		table.push_back(OPALUID[OPAL_UID::OPAL_LOCKINGSP_UID][i]);
	}
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	MsedCommand *cmd = new MsedCommand();
	session = new MsedSession(this);
	if (session->start(OPAL_UID::OPAL_ADMINSP_UID, password, OPAL_UID::OPAL_SID_UID)) {
		delete cmd;
		delete session;
		return 0xff;
	}
	//session[TSN:HSN]->LockingSP_UID.Get[Cellblock:[startColumn = LifeCycle,
	//                                               endColumn = LifeCycle]]
	if (getTable(table, 0x06, 0x06)) {
		LOG(E) << "Unable to determine LockingSP Lifecycle state";
		delete cmd;
		delete session;
		return 0xff;
	}
	// SL,SL,SN,NAME,Value
	//  0  1  2   3    4
	// verify response
	if ((0x06 != response.getUint8(3)) || // getlifecycle
		(0x08 != response.getUint8(4))) // Manufactured-Inactive
	{
		LOG(E) << "Locking SP lifecycle is not Manufactured-Inactive";
		delete cmd;
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] -> LockingSP_UID.Activate[]
	cmd->reset(OPAL_UID::OPAL_LOCKINGSP_UID, OPAL_METHOD::ACTIVATE);
	cmd->addToken(OPAL_TOKEN::STARTLIST);
	cmd->addToken(OPAL_TOKEN::ENDLIST);
	cmd->complete();
	if (session->sendCommand(cmd, response)) {
		delete cmd;
		delete session;
		return 0xff;
	}
	// method response code 00
	LOG(I) << "Locking SP Activate Complete";

	delete cmd;
	delete session;
	LOG(D1) << "Exiting MsedDevEnterprise::activatLockingSP()";
	return 0;
}
uint8_t MsedDevEnterprise::diskQuery()
{
	LOG(D1) << "Entering MsedDevEnterprise::diskQuery()" << dev;
	if (!isAnySSC()) {
		LOG(E) << "Device not OPAL 1,2 or Enterprise " << dev;
		return 1;
	}
	puke();
	LOG(D1) << "Exiting MsedDevEnterprise::diskQuery()" << dev;
	return 0;
}
uint8_t MsedDevEnterprise::takeOwnership(char * newpassword)
{
	LOG(D1) << "Entering MsedDevEnterprise::takeOwnership()";
	if (getDefaultPassword()) return 0xff;
	if (setSIDPassword((char *)response.getString(4).c_str(), newpassword, 0)) {
		LOG(I) << "takeownership failed";
		return 0xff;
	}
	LOG(I) << "takeownership complete";
	LOG(D1) << "Exiting takeOwnership()";
	return 0;
}
uint8_t MsedDevEnterprise::getDefaultPassword()
{
	LOG(D1) << "Entering MsedDevEnterprise::getDefaultPassword()";
	vector<uint8_t> hash;
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	//	Start Session
	session = new MsedSession(this);
	if (session->start(OPAL_UID::OPAL_ADMINSP_UID)) {
		LOG(E) << "Unable to start Unauthenticated session " << dev;
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] -> C_PIN_MSID_UID.Get[Cellblock : [startColumn = PIN,endColumn = PIN]]
	vector<uint8_t> table;
	table.push_back(0xa8);
	for (int i = 0; i < 8; i++) {
		table.push_back(OPALUID[OPAL_UID::OPAL_C_PIN_MSID][i]);
	}
	if (getTable(table, PIN, PIN)) {
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] <- EOS
	delete session;
	LOG(D1) << "Exiting getDefaultPassword()";
	return 0;
}
uint8_t MsedDevEnterprise::setSIDPassword(char * oldpassword, char * newpassword,
	uint8_t hasholdpwd, uint8_t hashnewpwd)
{
	vector<uint8_t> hash, table;
	LOG(D1) << "Entering MsedDevEnterprise::setSIDPassword()";
		if (!(isSupportedSSC())) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
		session = new MsedSession(this);
	if (!hasholdpwd) session->dontHashPwd();
	if (session->start(OPAL_UID::OPAL_ADMINSP_UID,
		oldpassword, OPAL_UID::OPAL_SID_UID)) {
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] -> C_PIN_SID_UID.Set[Values = [PIN = <new_SID_password>]]
	table.clear();
	table.push_back(0xa8);
	for (int i = 0; i < 8; i++) {
		table.push_back(OPALUID[OPAL_UID::OPAL_C_PIN_SID][i]);
	}
	hash.clear();
	if (hashnewpwd) {
		MsedHashPwd(hash, newpassword, this);
	}
	else {
		hash.push_back(0xd0);
		hash.push_back((uint8_t)strnlen(newpassword, 255));
		for (uint16_t i = 0; i < strnlen(newpassword, 255); i++) {
			hash.push_back(newpassword[i]);
		}
	}
	if (setTable(table, OPAL_TOKEN::PIN, hash)) {
		LOG(E) << "Unable to set new SID password ";
		delete session;
		return 0xff;
	}
	// session[TSN:HSN] <- EOS
	delete session;
	LOG(D1) << "Exiting MsedDevEnterprise::setSIDPassword()";
	return 0;
}

uint8_t MsedDevEnterprise::setTable(vector<uint8_t> table, OPAL_TOKEN name,
	OPAL_TOKEN value)
{
	vector <uint8_t> token;
	token.push_back((uint8_t) value);
	return(setTable(table, name, token));
}

uint8_t MsedDevEnterprise::setTable(vector<uint8_t> table, OPAL_TOKEN name, 
	vector<uint8_t> value)
{
	LOG(D1) << "Entering MsedDevEnterprise::setTable";
	MsedCommand *set = new MsedCommand();
	set->reset(OPAL_UID::OPAL_AUTHORITY_TABLE, OPAL_METHOD::SET);
	set->changeInvokingUid(table);
	set->addToken(OPAL_TOKEN::STARTLIST);
	set->addToken(OPAL_TOKEN::STARTNAME);
	set->addToken(OPAL_TOKEN::VALUES); // "values"
	set->addToken(OPAL_TOKEN::STARTLIST);
	set->addToken(OPAL_TOKEN::STARTNAME);
	set->addToken(name);
	set->addToken(value);
	set->addToken(OPAL_TOKEN::ENDNAME);
	set->addToken(OPAL_TOKEN::ENDLIST);
	set->addToken(OPAL_TOKEN::ENDNAME);
	set->addToken(OPAL_TOKEN::ENDLIST);
	set->complete();
	if (session->sendCommand(set, response)) {
		LOG(E) << "Set Failed ";
		delete set;
		return 0xff;
	}
	delete set;
	LOG(D1) << "Leaving MsedDevEnterprise::setTable";
	return 0;
}
uint8_t MsedDevEnterprise::getTable(vector<uint8_t> table, uint16_t startcol, 
	uint16_t endcol)
{
	LOG(D1) << "Entering MsedDevEnterprise::getTable";
	MsedCommand *get = new MsedCommand();
	get->reset(OPAL_UID::OPAL_AUTHORITY_TABLE, OPAL_METHOD::GET);
	get->changeInvokingUid(table);
	get->addToken(OPAL_TOKEN::STARTLIST);
	get->addToken(OPAL_TOKEN::STARTLIST);
	get->addToken(OPAL_TOKEN::STARTNAME);
	get->addToken(OPAL_TOKEN::STARTCOLUMN);
	get->addToken(startcol);
	get->addToken(OPAL_TOKEN::ENDNAME);
	get->addToken(OPAL_TOKEN::STARTNAME);
	get->addToken(OPAL_TOKEN::ENDCOLUMN);
	get->addToken(endcol);
	get->addToken(OPAL_TOKEN::ENDNAME);
	get->addToken(OPAL_TOKEN::ENDLIST);
	get->addToken(OPAL_TOKEN::ENDLIST);
	get->complete();
	if (session->sendCommand(get, response)) {
		delete get;
		return 0xff;
	}
	delete get;
	return 0;
}
uint16_t MsedDevEnterprise::comID()
{
    LOG(D1) << "Entering MsedDevEnterprise::comID()";
    if (disk_info.OPAL20)
        return disk_info.OPAL20_basecomID;
    else if (disk_info.OPAL10)
        return disk_info.OPAL10_basecomID;
    else if (disk_info.Enterprise)
        return disk_info.Enterprise_basecomID;
    else
        return 0x0000;
}

uint8_t MsedDevEnterprise::exec(MsedCommand * cmd, MsedResponse &response, uint8_t protocol)
{
    uint8_t rc = 0;
    OPALHeader * hdr = (OPALHeader *) cmd->getCmdBuffer();
    LOG(D3) << endl << "Dumping command buffer";
    IFLOG(D3) MsedHexDump(cmd->getCmdBuffer(), SWAP32(hdr->cp.length) + sizeof (OPALComPacket));
    rc = sendCmd(IF_SEND, protocol, comID(), cmd->getCmdBuffer(), IO_BUFFER_LENGTH);
    if (0 != rc) {
        LOG(E) << "Command failed on send " << (uint16_t) rc;
        return rc;
    }
    hdr = (OPALHeader *) cmd->getRespBuffer();
    do {
        //LOG(I) << "read loop";
        osmsSleep(25);
        memset(cmd->getRespBuffer(), 0, IO_BUFFER_LENGTH);
        rc = sendCmd(IF_RECV, protocol, comID(), cmd->getRespBuffer(), IO_BUFFER_LENGTH);

    }
    while ((0 != hdr->cp.outstandingData) && (0 == hdr->cp.minTransfer));
    LOG(D3) << std::endl << "Dumping reply buffer";
    IFLOG(D3) MsedHexDump(cmd->getRespBuffer(), SWAP32(hdr->cp.length) + sizeof (OPALComPacket));
    if (0 != rc) {
        LOG(E) << "Command failed on recv" << (uint16_t) rc;
        return rc;
    }
    response.init(cmd->getRespBuffer());
    return 0;
}


uint8_t MsedDevEnterprise::properties()
{
	LOG(D1) << "Entering MsedDevEnterprise::properties()";
	session = new MsedSession(this);  // use the session IO without starting a session
	MsedCommand *props = new MsedCommand(OPAL_UID::OPAL_SMUID_UID, OPAL_METHOD::PROPERTIES);
	props->addToken(OPAL_TOKEN::STARTLIST);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken(OPAL_TOKEN::HOSTPROPERTIES);
	//props->addToken("HostProperties");	
	props->addToken(OPAL_TOKEN::STARTLIST);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken("MaxComPacketSize");
	props->addToken(2048);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken("MaxPacketSize");
	props->addToken(2028);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken("MaxIndTokenSize");
	props->addToken(1992);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken("MaxPackets");
	props->addToken(1);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken("MaxSubpackets");
	props->addToken(1);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::STARTNAME);
	props->addToken("MaxMethods");
	props->addToken(1);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::ENDLIST);
	props->addToken(OPAL_TOKEN::ENDNAME);
	props->addToken(OPAL_TOKEN::ENDLIST);
	props->complete();
	if (session->sendCommand(props, propertiesResponse)) {
		delete props;
		return 0xff;
	}
	disk_info.Properties = 1;
	delete props;
	LOG(D1) << "Leaving MsedDevEnterprise::properties()";
	return 0;
}
/** Print out the Discovery 0 results */
void MsedDevEnterprise::puke()
{
	LOG(D1) << "Entering MsedDevEnterprise::puke()";
	/* IDENTIFY */
	cout << endl << dev << (disk_info.devType ? " OTHER " : " ATA ");
	cout << disk_info.modelNum << " " << disk_info.firmwareRev << " " << disk_info.serialNum << endl;
	/* TPer */
	if (disk_info.TPer) {
		cout << "TPer function (" << HEXON(4) << FC_TPER << HEXOFF << ")" << std::endl;
		cout << "    ACKNAK = " << (disk_info.TPer_ACKNACK ? "Y, " : "N, ")
			<< "ASYNC = " << (disk_info.TPer_async ? "Y, " : "N. ")
			<< "BufferManagement = " << (disk_info.TPer_bufferMgt ? "Y, " : "N, ")
			<< "comIDManagement  = " << (disk_info.TPer_comIDMgt ? "Y, " : "N, ")
			<< "Streaming = " << (disk_info.TPer_streaming ? "Y, " : "N, ")
			<< "SYNC = " << (disk_info.TPer_sync ? "Y" : "N")
			<< std::endl;
	}
	if (disk_info.Locking) {

		cout << "Locking function (" << HEXON(4) << FC_LOCKING << HEXOFF << ")" << std::endl;
		cout << "    Locked = " << (disk_info.Locking_locked ? "Y, " : "N, ")
			<< "LockingEnabled = " << (disk_info.Locking_lockingEnabled ? "Y, " : "N, ")
			<< "LockingSupported = " << (disk_info.Locking_lockingSupported ? "Y, " : "N, ");
		cout << "MBRDone = " << (disk_info.Locking_MBRDone ? "Y, " : "N, ")
			<< "MBREnabled = " << (disk_info.Locking_MBREnabled ? "Y, " : "N, ")
			<< "MediaEncrypt = " << (disk_info.Locking_mediaEncrypt ? "Y" : "N")
			<< std::endl;
	}
	if (disk_info.Geometry) {

		cout << "Geometry function (" << HEXON(4) << FC_GEOMETRY << HEXOFF << ")" << std::endl;
		cout << "    Align = " << (disk_info.Geometry_align ? "Y, " : "N, ")
			<< "Alignment Granularity = " << disk_info.Geometry_alignmentGranularity
			<< " (" << // display bytes
			(disk_info.Geometry_alignmentGranularity *
			disk_info.Geometry_logicalBlockSize)
			<< ")"
			<< ", Logical Block size = " << disk_info.Geometry_logicalBlockSize
			<< ", Lowest Aligned LBA = " << disk_info.Geometry_lowestAlignedLBA
			<< std::endl;
	}
	if (disk_info.Enterprise) {
		cout << "Enterprise function (" << HEXON(4) << FC_ENTERPRISE << HEXOFF << ")" << std::endl;
		cout << "    Range crossing = " << (disk_info.Enterprise_rangeCrossing ? "Y, " : "N, ")
			<< "Base comID = " << HEXON(4) << disk_info.Enterprise_basecomID
			<< ", comIDs = " << disk_info.Enterprise_numcomID << HEXOFF
			<< std::endl;
	}
	if (disk_info.OPAL10) {
		cout << "Opal V1.0 function (" << HEXON(4) << FC_OPALV100 << HEXOFF << ")" << std::endl;
		cout << "Base comID = " << HEXON(4) << disk_info.OPAL10_basecomID << HEXOFF
			<< ", comIDs = " << disk_info.OPAL10_numcomIDs
			<< std::endl;
	}
	if (disk_info.SingleUser) {
		cout << "SingleUser function (" << HEXON(4) << FC_SINGLEUSER << HEXOFF << ")" << std::endl;
		cout << "    ALL = " << (disk_info.SingleUser_all ? "Y, " : "N, ")
			<< "ANY = " << (disk_info.SingleUser_any ? "Y, " : "N, ")
			<< "Policy = " << (disk_info.SingleUser_policy ? "Y, " : "N, ")
			<< "Locking Objects = " << (disk_info.SingleUser_lockingObjects)
			<< std::endl;
	}
	if (disk_info.DataStore) {
		cout << "DataStore function (" << HEXON(4) << FC_DATASTORE << HEXOFF << ")" << std::endl;
		cout << "    Max Tables = " << disk_info.DataStore_maxTables
			<< ", Max Size Tables = " << disk_info.DataStore_maxTableSize
			<< ", Table size alignment = " << disk_info.DataStore_alignment
			<< std::endl;
	}

	if (disk_info.OPAL20) {
		cout << "OPAL 2.0 function (" << HEXON(4) << FC_OPALV200 << ")" << HEXOFF << std::endl;
		cout << "    Base comID = " << HEXON(4) << disk_info.OPAL20_basecomID << HEXOFF;
		cout << ", Initial PIN = " << HEXON(2) << disk_info.OPAL20_initialPIN << HEXOFF;
		cout << ", Reverted PIN = " << HEXON(2) << disk_info.OPAL20_revertedPIN << HEXOFF;
		cout << ", comIDs = " << disk_info.OPAL20_numcomIDs;
		cout << std::endl;
		cout << "    Locking Admins = " << disk_info.OPAL20_numAdmins;
		cout << ", Locking Users = " << disk_info.OPAL20_numUsers;
		cout << ", Range Crossing = " << (disk_info.OPAL20_rangeCrossing ? "Y" : "N");
		cout << std::endl;
	}
	if (disk_info.Unknown)
		cout << "**** " << (uint16_t)disk_info.Unknown << " **** Unknown function codes IGNORED " << std::endl;
	if (disk_info.Properties) {
		cout << std::endl << "TPer Properties: ";
		for (uint32_t i = 0; i < propertiesResponse.getTokenCount(); i++) {
			if (OPAL_TOKEN::STARTNAME == (OPAL_TOKEN) propertiesResponse.tokenIs(i)) {
				if (OPAL_TOKENID::OPAL_TOKENID_BYTESTRING != propertiesResponse.tokenIs(i + 1))
					cout << std::endl << "Host Properties: " << std::endl;
				else
					cout << "  " << propertiesResponse.getString(i + 1) << " = " << propertiesResponse.getUint64(i + 2);
				i += 2;
			}
			if (!(i % 6)) cout << std::endl;
		}
	}
}
uint8_t MsedDevEnterprise::dumpTable(char * password)
{
	CLog::Level() = CLog::FromInt(4);
	LOG(D1) << "Entering dumpTable()";
	vector<uint8_t> table, key, nextkey, temp;
	table.push_back(0xa8);
	for (int i = 0; i < 8; i++) {
		table.push_back(OPALUID[OPAL_UID::OPAL_AUTHORITY_TABLE][i]);
	}
	if (!isSupportedSSC()) {
		LOG(E) << "Device not Opal " << dev;
		return 0xff;
	}
	session = new MsedSession(this);
	// session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
	//                   HostChallenge = <Admin1_password>, HostSigningAuthority = Admin1_UID]
	if (session->start(OPAL_UID::OPAL_ADMINSP_UID, password, OPAL_UID::OPAL_SID_UID)) {
		delete session;
		return 0xff;
	}

	key.clear();
	while (true) {
		// Get the next UID
		if (nextTable(table, key)) {
			delete session;
			return 0xff;
		}
		key = response.getRawToken(2);
		nextkey = response.getRawToken(3);

		//AUTHORITY_TABLE.Get[Cellblock : [startColumn = 0, endColumn = 10]]

		if (getTable(key, 0, 4)) {
			delete session;
			return 0xff;
		}
		uint32_t i = 0;
		while (i < response.getTokenCount()) {
			if ((uint8_t)OPAL_TOKEN::STARTNAME == (uint8_t)response.tokenIs(i)) {
				LOG(I) << "col " << (uint32_t)response.getUint32(i + 1);
				temp = response.getRawToken(i + 2);
				MsedHexDump(temp.data(), (int)temp.size());
				i += 2;
			}
			i++;
		}

		if (9 != nextkey.size()) break; // no next row so bail
		key = nextkey;
	}
	delete session;
	return 0;
}

uint8_t MsedDevEnterprise::nextTable(std::vector<uint8_t> table,
	std::vector<uint8_t> startkey)
{
	LOG(D1) << "Entering nextTable";
	MsedCommand *next = new MsedCommand();
	next->reset(OPAL_UID::OPAL_AUTHORITY_TABLE, OPAL_METHOD::NEXT);
	next->changeInvokingUid(table);
	next->addToken(OPAL_TOKEN::STARTLIST);
	if (0 != startkey.size()) {
		next->addToken(OPAL_TOKEN::STARTNAME);
		next->addToken(OPAL_TINY_ATOM::UINT_00);
		//startkey[0] = 0x00;
		next->addToken(startkey);
		next->addToken(OPAL_TOKEN::ENDNAME);
	}
	next->addToken(OPAL_TOKEN::STARTNAME);
	next->addToken(OPAL_TINY_ATOM::UINT_01);
	next->addToken(OPAL_TINY_ATOM::UINT_02);
	next->addToken(OPAL_TOKEN::ENDNAME);
	next->addToken(OPAL_TOKEN::ENDLIST);
	next->complete();
	if (session->sendCommand(next, response)) {
		delete next;
		return 0xff;
	}
	delete next;
	return 0;
}
//uint8_t MsedDevEnterprise::revertnoerase(char * SIDPassword, char * Admin1Password)
//{
//    LOG(D1) << "Entering MsedDevEnterprise::revertnoerase";
//    if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
//		OPAL_TOKEN::READLOCKED, OPAL_TOKEN::OPAL_FALSE,
//                   Admin1Password)) {
//        LOG(E) << "revertnoerase failed - unable to unlock for read";
//        return 0xff;
//    }
//	if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
//		OPAL_TOKEN::WRITELOCKED, OPAL_TOKEN::OPAL_FALSE,
//                   Admin1Password)) {
//        LOG(E) << "revertnoerase failed - unable to unlock write";
//        return 0xff;
//    }
//	if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
//		OPAL_TOKEN::READLOCKENABLED, OPAL_TOKEN::OPAL_FALSE,
//                   Admin1Password)) {
//        LOG(E) << "revertnoerase failed - unable to disable readlocking";
//        return 0xff;
//    }
//	if (setLockingSPvalue(OPAL_UID::OPAL_LOCKINGRANGE_GLOBAL,
//		OPAL_TOKEN::WRITELOCKENABLED, OPAL_TOKEN::OPAL_FALSE,
//                   Admin1Password)) {
//        LOG(E) << "revertnoerase failed - unable to disable writelocking";
//        return 0xff;
//    }
//    if (revertLockingSP(Admin1Password, 1)) {
//        LOG(E) << "revertnoerase failed - unable to revert LockingSP (keepglobalrange) ";
//        return 0xff;
//    }
//    if (getDefaultPassword()) {
//        LOG(E) << "Unable to retrieve default SID password";
//        return 0xff;
//    }
//    if (setSIDPassword(SIDPassword, (char *) response.getString(4).c_str(), 1, 0)) {
//        LOG(I) << "revertnoerase failed unable to reset SID password";
//        return 0xff;
//    }
//    LOG(I) << "revertnoerase complete";
//    return 0;
//}