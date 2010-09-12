#include <new>
#include <engine/shared/config.h>
#include "player.h"


MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }
	
CPlayer::CPlayer(CGameContext *pGameServer, int CID, int Team)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	Character = 0;
	this->m_ClientID = CID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_CatchingTeam = -1;
	m_PrevCatchingTeam = -1;
	m_BaseCatchingTeam = -1;
	m_IsUsingCatchClient = false;
	m_IsJoined = false;
	m_DoesDamage = 0;
	m_HasTeam = true;
	m_NoBroadcast = 0;
	m_TickBroadcast = false;
	m_Colorassign = 0;
	m_CaughtBy = -1;
}

CPlayer::~CPlayer()
{
	delete Character;
	Character = 0;
}

void CPlayer::Tick()
{
	Server()->SetClientScore(m_ClientID, m_Score);
	
	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}
	if(!Character && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
		m_Spawning = true;
	
	if(!GameServer()->m_pController->JoiningSystem() && !m_IsJoined)
		m_IsJoined = true;
	
	if(GameServer()->m_pController->IsCatching() && m_Team != -1)
	{
		char aBuf[512];
		// Increasing Score if ppl does VAR (Optimal: 20) or more damage
		if(m_DoesDamage >= g_Config.m_SvDamagePoint)
		{
			m_Score++;
			m_DoesDamage -= g_Config.m_SvDamagePoint;
		}
		
		// Collorassign
		int UsedColor[MAX_CLIENTS] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		int NumPlayers = 0;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != -1 && GameServer()->m_apPlayers[i]->m_BaseCatchingTeam != -1)
				UsedColor[GameServer()->m_apPlayers[i]->m_BaseCatchingTeam] = 1;
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != -1 && GameServer()->m_apPlayers[i]->m_IsJoined)
				NumPlayers++;
		}

		if(m_Colorassign && !m_IsJoined)
		{
			m_Colorassign--;
			int left = m_Colorassign/Server()->TickSpeed();
			//int mili = m_Colorassign*1000/left/Server()->TickSpeed() - 1000;
			str_format(aBuf, sizeof(aBuf),  "%d Seconds left to select a team.", left);
			GameServer()->SendBroadcast(aBuf, m_ClientID);
			m_AssignColor = true;
		}
		else if(m_AssignColor && !m_IsJoined)
		{
			int Color = -1;

			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!UsedColor[i])
				{
					Color = i;
					break;
				}
			}
			m_BaseCatchingTeam = Color;

			if(NumPlayers < g_Config.m_SvCheatProtection)
			{
				m_IsJoined = true;
				GameServer()->SendBroadcast("", m_ClientID);
				if(GetCharacter())
					GetCharacter()->CreateDieExplosion(false);
			}
			else
				GameServer()->SendChatTarget(m_ClientID, "Please wait until this round ends");

			GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);
			GameServer()->SendChatTarget(m_ClientID, "You got a random color");
			m_AssignColor = false;
		}
		else if(!m_IsJoined && NumPlayers >= g_Config.m_SvCheatProtection)
			GameServer()->SendBroadcast("Please wait until this round ends", m_ClientID);

		//Strange bug
		if(!GameServer())
			return;

		// Teambroadcast
		if(Server()->Tick()%Server()->TickSpeed()/2 == 0)
		{
			if(m_IsJoined && m_Colorassign)
			{
				m_Colorassign = 0;
				m_AssignColor = false;
			}

			int NumPlayers = 0;
			int TeamPlayers = 0;
			int OtherTeam = 0;
			int OtherOwner = -1;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != -1 && GameServer()->m_apPlayers[i]->m_IsJoined)
				{
					NumPlayers++;
					if(GameServer()->m_apPlayers[i]->m_CatchingTeam == m_BaseCatchingTeam)
						TeamPlayers++;
					else if(GameServer()->m_apPlayers[i]->m_CatchingTeam == m_CatchingTeam)
						OtherTeam++;
					if(GameServer()->m_apPlayers[i]->m_BaseCatchingTeam == m_CatchingTeam)
						OtherOwner = i;
				}
			}
			if((TeamPlayers > 0 || m_HasTeam) && !m_NoBroadcast && m_Team != -1 && m_IsJoined && NumPlayers > 2)
			{
				if(TeamPlayers == 0 && m_HasTeam)
				{
					m_HasTeam = false;
					m_NoBroadcast = Server()->TickSpeed() * 3;
					m_TickBroadcast = true;
					GameServer()->SendChatTarget(m_ClientID, "You lose your Team");
					GameServer()->SendBroadcast("You lose your Team", m_ClientID);
				}
				else
				{
					char Buf[128];
					str_format(Buf, sizeof(Buf),  "Your Team: %d / %d Player", TeamPlayers, NumPlayers);
					GameServer()->SendBroadcast(Buf, m_ClientID);
				}
			}
			else if(!m_HasTeam && OtherTeam > 0 && TeamPlayers == 0 && !m_NoBroadcast && m_Team != -1 && m_IsJoined && NumPlayers > 2)
			{
				char Buf[128];
				if(OtherOwner > -1)
					str_format(Buf, sizeof(Buf),  "%s's Team: %d / %d Player", Server()->ClientName(OtherOwner), OtherTeam, NumPlayers);
				else
					str_format(Buf, sizeof(Buf),  "Team: %d / %d Player", OtherTeam, NumPlayers);
				GameServer()->SendBroadcast(Buf, m_ClientID);
			}
		}

		if(m_TickBroadcast && m_NoBroadcast > 0)
			m_NoBroadcast--;
		else if(m_TickBroadcast)
			m_TickBroadcast = false;
	}
	else if(GameServer()->m_pController->IsZCatch())
	{
		char aBuf[512];
		int Total = 0, Num = 0;
		GameServer()->m_pController->SetColor(this);
		if(m_CaughtBy == -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i != m_ClientID && GameServer()->m_apPlayers[i])
				{
					if(GameServer()->m_apPlayers[i]->GetTeam() != -1)
						Total++;
					if(GameServer()->m_apPlayers[i]->m_CaughtBy == m_ClientID)
						Num++;
				}
			}
			str_format(aBuf, sizeof(aBuf), "(%d/%d)", Num, Total);
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i != m_CaughtBy && GameServer()->m_apPlayers[i])
				{
					if(GameServer()->m_apPlayers[i]->GetTeam() != -1)
						Total++;
					if(GameServer()->m_apPlayers[i]->m_CaughtBy == m_CaughtBy)
						Num++;
				}
			}
			str_format(aBuf, sizeof(aBuf), "Caught by %s (%d/%d)", Server()->ClientName(m_CaughtBy), Num, Total);
		}
		GameServer()->SendBroadcast(aBuf, m_ClientID);
	}
	
	if(Character)
	{
		if(Character->IsAlive())
		{
			m_ViewPos = Character->m_Pos;
		}
		else
		{
			delete Character;
			Character = 0;
		}
	}
	else if(m_Spawning && m_RespawnTick <= Server()->Tick())
		TryRespawn();
}

void CPlayer::Snap(int SnappingClient)
{
	CNetObj_ClientInfo *ClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, m_ClientID, sizeof(CNetObj_ClientInfo)));
	StrToInts(&ClientInfo->m_Name0, 6, Server()->ClientName(m_ClientID));
	StrToInts(&ClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
	ClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	ClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	ClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	CNetObj_PlayerInfo *Info = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));

	Info->m_Latency = m_Latency.m_Min;
	Info->m_LatencyFlux = m_Latency.m_Max-m_Latency.m_Min;
	Info->m_Local = 0;
	Info->m_ClientId = m_ClientID;
	Info->m_Score = m_Score;
	Info->m_Team = m_Team;

	if(m_ClientID == SnappingClient)
		Info->m_Local = 1;	
}

void CPlayer::OnDisconnect()
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf),  "\"%s\" has left the game", Server()->ClientName(m_ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", m_ClientID, Server()->ClientName(m_ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	if(Character)
		Character->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(Character)
		Character->OnDirectInput(NewInput);

	if(!Character && m_Team >= 0 && (NewInput->m_Fire&1))
		m_Spawning = true;
	
	if(!Character && m_Team == -1)
		m_ViewPos = vec2(NewInput->m_TargetX, NewInput->m_TargetY);
}

CCharacter *CPlayer::GetCharacter()
{
	if(Character && Character->IsAlive())
		return Character;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(Character)
	{
		Character->Die(m_ClientID, Weapon);
		delete Character;
		Character = 0;
	}
}

void CPlayer::Respawn()
{
	if(m_Team > -1)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team)
{
	// clamp the team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;
		
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "\"%s\" joined the %s", Server()->ClientName(m_ClientID), GameServer()->m_pController->GetTeamName(Team));
	GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf); 
	
	KillCharacter();

	m_Team = Team;
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	
	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos = vec2(100.0f, -60.0f);
	
	if(!GameServer()->m_pController->CanSpawn(this, &SpawnPos))
		return;

	// check if the position is occupado
	CEntity *apEnts[2] = {0};
	int NumEnts = GameServer()->m_World.FindEntities(SpawnPos, 64, apEnts, 2, NETOBJTYPE_CHARACTER);
	
	if(NumEnts == 0)
	{
		m_Spawning = false;
		Character = new(m_ClientID) CCharacter(&GameServer()->m_World);
		Character->Spawn(this, SpawnPos);
		GameServer()->CreatePlayerSpawn(SpawnPos, m_ClientID);
	}
}
