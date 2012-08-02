/* 
 * Copyright (C) 2012 Yee Young Han <websearch@naver.com> (http://blog.naver.com/websearch)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

bool CSipUserAgent::RecvCancelRequest( int iThreadId, CSipMessage * pclsMessage )
{
	std::string strCallId;
	char	szTag[SIP_TAG_MAX_SIZE];

	if( pclsMessage->GetCallId( strCallId ) == false ) return false;
	SipMakeTag( szTag, sizeof(szTag) );

	CSipMessage * pclsResponse = pclsMessage->CreateResponse( SIP_OK, szTag );
	if( pclsResponse )
	{
		gclsSipStack.SendSipMessage( pclsResponse );
		pclsResponse = NULL;
	}

	SIP_DIALOG_MAP::iterator		itMap;

	m_clsMutex.acquire();
	itMap = m_clsMap.find( strCallId );
	if( itMap != m_clsMap.end() )
	{
		if( itMap->second.m_sttStartTime.tv_sec == 0 )
		{
			pclsResponse = itMap->second.m_pclsInvite->CreateResponse( SIP_REQUEST_TERMINATED );
			gettimeofday( &itMap->second.m_sttEndTime, NULL );

			m_clsMap.erase( itMap );
		}
	}
	m_clsMutex.release();

	if( pclsResponse )
	{
		gclsSipStack.SendSipMessage( pclsResponse );
		m_pclsCallBack->EventCallEnd( strCallId.c_str(), SIP_OK );
	}

	return true;
}
