#pragma once

#include "XGetopt.h"
#include "Logger.h"

typedef enum {
	Unspecified,
	Client,
	Server
} Mode;

typedef enum {
	Both,
	Network,
	Disk
} Operation;

typedef enum {
	RawDisk,
	Snapshot,
	File
} DiskSource;

	
#define DEFAULT_OUTSTANDING_OP_LENGTH 4
#define DEFAULT_CHUNK_SIZE 262144 //256k

class Settings
{
public:

	Settings(void)
	{
		m_mode = Unspecified;
		m_port = 12345;
		m_op = Both;
		m_dataLength = 0;
		m_chunkSize = DEFAULT_CHUNK_SIZE;
		m_opLength = DEFAULT_OUTSTANDING_OP_LENGTH;
		m_tcpBufSize = 65535;
		m_useTransmitFunc = false;
		m_diskReadSource = File;
	}

	~Settings(void)
	{
	}

	bool ParseCommandLine(int argc, TCHAR* argv[]) {
		int c;

		while ((c = getopt(argc, argv, _T("c:sa:v:f:p:ndl:k:o:b:t"))) != EOF) {
			switch (c) {
			case _T('c'):
				m_mode = Client;
				m_targetHost = optarg;
				break;

			case _T('s'):
				m_mode = Server;
				break;

			case _T('a'):
				m_diskReadSource = Snapshot;
				m_sourceFile = optarg;
				break;

			case _T('v'):
				m_diskReadSource = RawDisk;
				m_sourceFile = optarg;
				break;

			case _T('f'):
				m_diskReadSource = File;
				m_sourceFile = optarg;
				break;

			case _T('p'):
				m_port = static_cast<UINT32>(::_ttoi(optarg));
				break;

			case _T('n'):
				m_op = Network;
				break;

			case _T('d'):
				m_op = Disk;
				break;

			case _T('l'):
				m_dataLength = ParseByteCount(optarg);
				break;

			case _T('b'):
				m_tcpBufSize = static_cast<DWORD>(ParseByteCount(optarg));
				break;

			case _T('k'):
				m_chunkSize = static_cast<DWORD>(ParseByteCount(optarg));
				break;

			case _T('o'):
				m_opLength = ::_ttoi(optarg);
				break;

			case _T('t'):
				m_useTransmitFunc = true;
				break;

			case _T('?'):
				Logger::Error(_T("Unrecognized parameter %s\n"), optarg);
				break;

			default:
				Logger::Error(_T("Unrecognized option %c\n"), c);
			}
		}

		if (m_mode == Unspecified) {
			Logger::Error(_T("You must specify server or client mode"));
			return false;
		}

		if (m_opLength < 1) {
			Logger::Error(_T("You must specify an op length of at least one"));
			return false;
		}

		if (m_chunkSize < 1) {
			Logger::Error(_T("You must specify a chunk size of at least one byte"));
			return false;
		}

		if (m_mode == Client) {
			if (m_op != Disk && m_targetHost.length() == 0) {
				Logger::Error(_T("You must specify a target host"));
				return false;
			}

			if (m_op != Network && m_sourceFile.length() == 0) {
				Logger::Error(_T("You must specify a source file"));
				return false;
			}

			if (m_op == Network && m_dataLength <= 0) {
				Logger::Error(_T("When in network-only mode, a non-zero send length must be specified"));
				return false;
			}

			if (m_op == Disk && m_useTransmitFunc) {
				Logger::Error(_T("The Transmit* functions are not available in disk-only mode"));
				return false;
			}
		} else if (m_mode == Server) {
			if (m_op == Disk && m_dataLength <= 0) {
				Logger::Error(_T("When in disk-only mode, a non-zero data length must be specified"));
				return false;
			}

			if (m_useTransmitFunc) {
				Logger::Error(_T("You cannot use the Transmit(File|Packets) functions in server mode"));
				return false;
			}
		}

		return true;
	}

	void DumpOptions() {
		_tprintf(_T("Options: \n\
  Mode: %s\n\
  Ops: %s\n\
  Hostname: %s\n\
  Port: %d\n\
  Filename: %s\n\
  Disk I/O Source: %s\n\
  Chunk size: %d\n\
  Op Count: %d\n\
  Data length: %I64d\n\
  TCP buf size: %d\n\
"),
		m_mode == Client ? _T("Client") : _T("Server"),
		m_op == Both ? _T("File/Network") : (m_op == Disk ? _T("File") : _T("Network")),
		m_targetHost.c_str(),
		m_port,
		m_sourceFile.c_str(),
		m_diskReadSource == File ? _T("Filesystem file") : (m_diskReadSource == RawDisk ? _T("Raw Disk") : _T("VSS Disk Snapshot")),
		m_chunkSize,
		m_opLength,
		m_dataLength,
		m_tcpBufSize);
	}

	static void PrintUsage() {
		_tprintf(_T("Usage: \n\
AsyncIoTest.exe (-c hostname | -s) options \n\
\n\
Options:\n\
  -c hostname - Run in client mode, connecting to the server at hostname\n\
  -s - Run in server mode, waiting for connections\n\
  -p port - Connect (client) or listen (server) on this port.  Default 12345\n\
  -f file - The file to send to the server. Required unless -n or -s specified.\n\
  -a volume - Volume to snapshot and send to the server. Required unless -n or -s specified.\n\
  -v volume - Volume to read and send to the server. Required unless -n or -s specified.\n\
  -n - Run in network only mode;do not read (client) or write (server) the disk\n\
  -d - Run in disk only mode;do not write (client) or read (server) the network\n\
  -k chunksize[k|m|g|K|M|G] - The size of chunks to read and write with\n\
  -o oplength - The number of async ops to keep outstanding.  Default 2.\n\
  -l datalength - The number of bytes to transfer, if -n is specified\n\
  -b bufsize - The size of the SO_SNDBUF/SO_RECVBUF socket buffers.\n\
  -t - In client mode, uses Transmit(File|Packets) to send data\n\
\n\
  "));
	}

	Mode getMode() {return m_mode;}
	Operation getOperation() {return m_op;}
	DiskSource getDiskReadSource() {return m_diskReadSource;}
	const tstring& getTargetHost() {return m_targetHost;}
	const tstring& getSourceFile() {return m_sourceFile;}
	UINT32 getPort() {return m_port;}
	unsigned __int64 getDataLength() {return m_dataLength;}
	DWORD getChunkSize() {return m_chunkSize;}
	int getOpLength() {return m_opLength;}
	int getTcpBufSize() {return m_tcpBufSize;}
	bool getUseTransmitFunc() {return m_useTransmitFunc;}

private:
	Mode m_mode;
	Operation m_op;
	DiskSource m_diskReadSource;
	tstring m_targetHost;
	tstring m_sourceFile;
	UINT32 m_port;
	unsigned __int64 m_dataLength;
	DWORD m_chunkSize;
	int m_opLength;
	int m_tcpBufSize;
	bool m_useTransmitFunc;

	unsigned __int64 ParseByteCount(LPCTSTR count) {
		//Look for a decimal number, optionally followed by a scale indicator of K, M, or G
		return byte_atoi(count);
	}

	static const long kKilo_to_Unit = 1024;
	static const long kMega_to_Unit = 1024 * 1024;
	static const long kGiga_to_Unit = 1024 * 1024 * 1024;

	static const long kkilo_to_Unit = 1000;
	static const long kmega_to_Unit = 1000 * 1000;
	static const long kgiga_to_Unit = 1000 * 1000 * 1000;

	unsigned __int64 byte_atoi( const TCHAR *inString ) {
		double theNum = 0;
		TCHAR suffix = '\0';

		/* scan the number and any suffices */
		::_stscanf( inString, _T("%lf%c"), &theNum, &suffix );

		/* convert according to [Gg Mm Kk] */
		switch ( suffix ) {
			case 'G': theNum *= kGiga_to_Unit;  break;
			case 'M': theNum *= kMega_to_Unit;  break;
			case 'K':  theNum *= kKilo_to_Unit;  break;
			case 'g':  theNum *= kgiga_to_Unit;  break;
			case 'm':  theNum *= kmega_to_Unit;  break;
			case 'k':  theNum *= kkilo_to_Unit;  break;
			default: break;
		}
		return (unsigned __int64) theNum;
	} /* end byte_atof */
};
