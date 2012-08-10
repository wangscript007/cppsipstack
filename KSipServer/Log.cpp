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

#include "SipParserDefine.h"
#include "ServerUtility.h"
#include <string.h>
#include <stdarg.h>
#include "Log.h"
#include "Directory.h"

#ifdef WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

// CLog 클래스의 정적 변수를 초기화 한다.
char * CLog::m_pszDirName = NULL;
char CLog::m_szDate[9] = { '\0' };
FILE * CLog::m_sttFd = NULL;
CSipMutex * CLog::m_pThreadMutex = NULL;
int CLog::m_iLevel = LOG_ERROR;
int CLog::m_iMaxLogSize = 0;
int CLog::m_iLogSize = 0;
int CLog::m_iIndex = 1;

/** 로그 파일을 저장할 디렉토리를 설정한다.
 *	만약 thread mutex 가 설정되어 있지 않으면 쓰레드 mutex 를 생성한다.
 *
 *	@param	pszDirName	[in] 로그 파일을 저장할 디렉토리
 *	@return	성공하면 0 을 리턴한다.
 *			실패하면 -1 을 리턴한다.
 */

int CLog::SetDirectory( const char * pszDirName )
{
	if( pszDirName == NULL ) return -1;

	if( m_pThreadMutex == NULL )
	{
		m_pThreadMutex = new CSipMutex();
		if( m_pThreadMutex == NULL ) return -1;
	}

	if( m_pThreadMutex->acquire() == false ) return -1;

	if( m_pszDirName == NULL )
	{
		int	iLen = (int)strlen(pszDirName);
		m_pszDirName = new char[ iLen + 1 ];
		if( m_pszDirName == NULL ) return -1;

		sprintf( m_pszDirName, "%s", pszDirName );
#ifdef WIN32
		if( m_pszDirName[iLen-1] == '\\' )
#else
		if( m_pszDirName[iLen-1] == '/' )
#endif
		{
			m_pszDirName[iLen-1] = '\0';
		}
	}

	m_pThreadMutex->release();

	if( CDirectory::Create( m_pszDirName ) != 0 ) return -1;

	return 0;
}

/** 클래스 변수를 초기화시킨다. */
int CLog::Release()
{
	if( CLog::m_pszDirName )
	{
		delete [] CLog::m_pszDirName;
		CLog::m_pszDirName = NULL;
	}

	if( CLog::m_pThreadMutex )
	{
		delete CLog::m_pThreadMutex;
		CLog::m_pThreadMutex = NULL;
	}

	return 0;
}

/** 로그 파일에 로그를 저장한다.
 *
 *	@param	iLevel	[in] 로그 파일 레벨
 *	@param	fmt		[in] 로그 파일에 저장할 포맷 문자열
 *	@param	...		[in] fmt 포맷에 입력할 인자들
 *	@return	입력에 성공하면 0 을 리턴한다.
 *			입력값이 잘못된 경우에는 -1을 리턴한다.
 *			mutex lock 에 실패한 경우에는 -1을 리턴한다.
 *			로그 파일 Open 에 실패하면 -1을 리턴한다.
 *			로그 레벨이 설정된 로그 레벨과 일치하지 않는 경우에는 1을 리턴한다.
 */

int CLog::Print( EnumLogLevel iLevel, const char * fmt, ... )
{
	if( ( m_iLevel & iLevel ) == 0 ) return 1;

	va_list		ap;
	char		szBuf[LOG_MAX_SIZE];
	char		szDate[9], szHeader[30];
	int			iResult = 0;
	struct tm	sttTm;

	switch( iLevel )
	{
	case LOG_ERROR:
		snprintf( szHeader, sizeof(szHeader), "[ERROR] " );
		break;
	case LOG_INFO:
		snprintf( szHeader, sizeof(szHeader), "[INFO] " );
		break;
	case LOG_DEBUG:
		snprintf( szHeader, sizeof(szHeader), "[DEBUG] " );
		break;
	case LOG_NETWORK:
		snprintf( szHeader, sizeof(szHeader), "[NETWORK] " );
		break;
	case LOG_SYSTEM:
		snprintf( szHeader, sizeof(szHeader), "[SYSTEM] " );
		break;
	case LOG_SQL:
		snprintf( szHeader, sizeof(szHeader), "[SQL] " );
		break;
	default:
		memset( szHeader, 0, sizeof(szHeader) );
		break;
	}
	
	// 현재의 시간을 구한다.
	struct timeval	sttTime;

	gettimeofday( &sttTime, NULL );
#ifdef WIN32
	_localtime32_s( &sttTm, &sttTime.tv_sec );
#else
	localtime_r( &sttTime.tv_sec, &sttTm );	
#endif

	snprintf( szDate, sizeof(szDate), "%04d%02d%02d"
		, sttTm.tm_year + 1900, sttTm.tm_mon + 1, sttTm.tm_mday );

	if( m_pThreadMutex && m_pThreadMutex->acquire() == false ) return -1;

	if( m_pszDirName && strcmp( m_szDate, szDate ) )
	{
		// 현재의 날짜와 클래스에 저장된 날짜가 다른 경우에는 로그 파일을 새로
		// 제작하여서 Open 한다.
		char	szFileName[FULLPATH_FILENAME_MAX_SIZE];
		
		m_iIndex = 1;

OPEN_FILE:
		m_iLogSize = 0;
#ifdef WIN32
		snprintf( szFileName, sizeof(szFileName), "%s\\%04d%02d%02d_%d.txt"
			, m_pszDirName, sttTm.tm_year + 1900, sttTm.tm_mon + 1, sttTm.tm_mday
			, m_iIndex );
#else
		snprintf( szFileName, sizeof(szFileName), "%s/%04d%02d%02d_%d.txt"
			, m_pszDirName, sttTm.tm_year + 1900, sttTm.tm_mon + 1, sttTm.tm_mday
			, m_iIndex );
#endif
		
		// 이미 Open 된 파일이 있는 경우에는 이를 닫는다.
		if( m_sttFd ) fclose( m_sttFd );

		if( m_iMaxLogSize > 0 )
		{
#ifdef WIN32
			HANDLE	hFile;

			hFile = CreateFile( szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
			if( hFile != INVALID_HANDLE_VALUE )
			{
				DWORD dwFileSize, dFileSizeHigh;
				
				dwFileSize = ::GetFileSize( hFile, &dFileSizeHigh );
				CloseHandle( hFile );

				if( dwFileSize != 0xFFFFFFFF && dwFileSize >= (DWORD)m_iMaxLogSize )
				{
					++m_iIndex;
					goto OPEN_FILE;
				}

				m_iLogSize = dwFileSize;
			}
#else
			struct  stat    sttStat;

			if( lstat( szFileName, &sttStat ) == 0 )
			{
				if( sttStat.st_size >= m_iMaxLogSize )
				{
					++m_iIndex;
					goto OPEN_FILE;
				}

				m_iLogSize = sttStat.st_size;
			}
#endif
		}
		
		m_sttFd = fopen( szFileName, "a" );
		if( m_sttFd == NULL )
		{
			printf( "log file(%s) open error\n", szFileName );
			iResult = -1;
		}

		snprintf( m_szDate, sizeof(m_szDate), "%s", szDate );
	}
	else if( m_iMaxLogSize > 0 && m_iLogSize >= m_iMaxLogSize )
	{
		++m_iIndex;
		goto OPEN_FILE;
	}

	if( m_pThreadMutex ) m_pThreadMutex->release();

	va_start( ap, fmt );
	vsnprintf( szBuf, sizeof(szBuf)-1, fmt, ap );
	va_end( ap );

	if( m_pThreadMutex && m_pThreadMutex->acquire() == false ) return -1;

	if( m_sttFd )
	{
#ifdef WIN32
		m_iLogSize += fprintf( m_sttFd, "[%02d:%02d:%02d.%06u] %s[%u] %s\n"
			, sttTm.tm_hour, sttTm.tm_min, sttTm.tm_sec, sttTime.tv_usec, szHeader, GetCurrentThreadId(), szBuf );
#else
		m_iLogSize += fprintf( m_sttFd, "[%02d:%02d:%02d.%06u] %s[%u] %s\n"
			, sttTm.tm_hour, sttTm.tm_min, sttTm.tm_sec, (unsigned int)sttTime.tv_usec, szHeader, (unsigned int)pthread_self(), szBuf );
#endif
		fflush( m_sttFd );
	}
	else
	{
#ifdef WIN32
		printf( "[%02d:%02d:%02d] %s%s\r\n"
			, sttTm.tm_hour, sttTm.tm_min, sttTm.tm_sec, szHeader, szBuf );
#else
		printf( "[%02d:%02d:%02d] %s%s\n"
			, sttTm.tm_hour, sttTm.tm_min, sttTm.tm_sec, szHeader, szBuf );
#endif
	}

	if( m_pThreadMutex ) m_pThreadMutex->release();

	return iResult;
}

/**	로그 파일에 저장할 로그 레벨을 설정한다. 
 *
 *	여러 로그를 저장할 경우, '|' 연산자를 이용하여서 여러 로그 레벨을 설정할 수 있다.
 *
 *	@param	iLevel	[in] 디버그 로그를 저장할 경우, LOG_DEBUG 를 설정한다.
 *					정보 로그를 저장할 경우, LOG_INFO 를 설정한다.
 *					에러 로그를 저장할 경우, LOG_ERROR 를 설정한다.
 *	@return	0 을 리턴한다.
 */

int CLog::SetLevel( int iLevel )
{
	CLog::m_iLevel = LOG_ERROR | LOG_SYSTEM;
	CLog::m_iLevel |= iLevel;

	return 0;
}

/** 로그 파일에 저장할 로그 레벨을 모두 삭제한다.
 *	- 로그 파일에 로그가 저장되지 않는다.

 *	@return	0 을 리턴한다.
 */

int CLog::SetNullLevel()
{
	CLog::m_iLevel = 0;

	return 0;
}

/** 디버그 로그 레벨을 설정한다.
 *
 *	@return	0 을 리턴한다.
 */

int CLog::SetDebugLevel( )
{
	CLog::m_iLevel |= LOG_DEBUG;

	return 0;
}

/** 입력한 로그 레벨이 현재 출력할 수 있는 로그 레벨인지 분석하여 준다.
 *
 *	@param	iLevel	[in] 로그 레벨
 *	@return	현재 출력할 수 있는 로그 레벨인 경우에는 0 을 리턴한다.
 *			그렇지 않으면 -1 을 리턴한다.
 */

bool CLog::IsPrintLogLevel( EnumLogLevel iLevel )
{
	if( ( CLog::m_iLevel & iLevel ) == 0 ) return false;

	return true;
}

/** 로그를 저장할 최대 파일 크기를 설정한다. */
void CLog::SetMaxLogSize( int iSize )
{
	if( iSize < MIN_LOG_FILE_SIZE )
	{
		iSize = MIN_LOG_FILE_SIZE;
	}
	else if( iSize > MAX_LOG_FILE_SIZE )
	{
		iSize = MAX_LOG_FILE_SIZE;
	}
	m_iMaxLogSize = iSize;
}

/** 로그파일의 인덱스 번호 */
int CLog::GetLogIndex()
{
	return m_iIndex;
}
