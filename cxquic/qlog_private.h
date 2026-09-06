#pragma once

// qlog output (draft-ietf-quic-qlog-main-schema): one file per connection, one JSON object per
// event, in the JSON-SEQ framing the qvis tools read.
//
// Nothing here is on a hot path by design -- a connection with no log writes nothing and pays one
// null check per event -- but a connection with one writes a line per packet, so this is a
// debugging switch rather than something to leave on.

#include "conn_private.h"

CX_C_BEGIN

typedef struct QuicQlog QuicQlog;

// Turns logging on for connections created after this call, writing into `dir`, which must
// already exist. An empty or NULL directory turns it off again.
void _quicQlogDir(_In_opt_ strref dir);
_Pure bool _quicQlogEnabled(void);

// Opens the file for one connection and writes the header. `odcid` names it, which is the
// convention every other implementation follows, so the two ends of a handshake produce files
// that sort together.
_Ret_maybenull_ QuicQlog* _quicQlogCreate(_In_ const QuicCid* odcid, bool server);
void _quicQlogDestroy(_Inout_ QuicQlog** ql);

// Every event takes the log by pointer and does nothing when it is NULL, so a caller never has to
// ask whether logging is on.
void _quicQlogStarted(_Inout_opt_ QuicQlog* ql, _In_ const NetAddr* peer);
void _quicQlogClosed(_Inout_opt_ QuicQlog* ql, uint64 error, bool app, bool local,
                     _In_opt_ strref reason);
void _quicQlogParams(_Inout_opt_ QuicQlog* ql, _In_ const QuicTransportParams* tp, bool local);

// A packet that was sent or received, with its frames decoded out of `payload` by the same codec
// that put them there.
void _quicQlogPacket(_Inout_opt_ QuicQlog* ql, bool sent, _In_ const QuicPktHdr* h, size_t len,
                     _In_reads_bytes_(plen) const uint8* payload, size_t plen);

// A packet that was thrown away before its frames could be read, and why.
void _quicQlogDropped(_Inout_opt_ QuicQlog* ql, _In_ strref trigger, size_t len);

void _quicQlogMetrics(_Inout_opt_ QuicQlog* ql, _In_ const QuicRecovery* r);
void _quicQlogLost(_Inout_opt_ QuicQlog* ql, int sp, uint64 pn);

CX_C_END
