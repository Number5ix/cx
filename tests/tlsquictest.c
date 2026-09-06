// The QUIC-profile TLS 1.3 handshake engine, driven end to end between a cx client and a cx server
// with no sockets involved.
//
// Every test here runs a real handshake: the two engines are wired to each other through the
// TlsQuicHandlers table, and the harness shuttles CRYPTO bytes between them until neither has
// anything left to say. What that buys is that the traffic secrets are checked for agreement --
// each side's write secret against the other's read secret, at every encryption level -- which
// nothing short of a complete handshake can establish.

#include <cxtls.h>

#include "../cxtls/tls13_private.h"
#include "../cxtls/tls_private.h"

#include "tlstestcert.h"

#define TEST_FILE  tlsquictest
#define TEST_FUNCS tlsquictest_funcs
#include "common.h"

// bool CHECK(const char *what, bool cond);
//
// Assigns 1 to `ret` and jumps to `out` when the condition does not hold.
#define CHECK(what, cond)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            TEST_FAILV(ret, 1, _SL("${string}"), stvar(strref, _S what));         \
            goto out;                                                            \
        }                                                                        \
    } while (0)

// bool CHECK_U(const char *what, int64 got, int64 want);
#define CHECK_U(what, got, want)                                                 \
    do {                                                                         \
        if ((int64)(got) != (int64)(want)) {                                     \
            TEST_FAILV(ret, 1, _SL("${string}: got ${int}, expected ${int}"),    \
                       stvar(strref, _S what), stvar(int64, (int64)(got)),       \
                       stvar(int64, (int64)(want)));                             \
            goto out;                                                            \
        }                                                                        \
    } while (0)

// ---------------------------------------------------------------------------------------------
// One end of a handshake
// ---------------------------------------------------------------------------------------------

typedef struct QPeer {
    TlsQuic* tq;

    // Handshake bytes this end has produced and not yet handed to the other, per level.
    Buffer out[4];

    uint8 rd[4][TLS13_MAX_HASH];
    uint8 wr[4][TLS13_MAX_HASH];
    bool haveRd[4];
    bool haveWr[4];
    uint16 suite;
    size_t secretLen;

    Buffer peerTp;
    bool complete;
    bool failed;
    uint8 alert;

    // 0-RTT, as the transport under the handshake sees it.
    bool earlyAsked;      // earlyParams() was called
    bool earlyRefuse;     // answer it with no, which is a transport declining to use 0-RTT
    bool earlyDecided;    // earlyData() was called
    bool earlyAccepted;
    Buffer earlyTp;       // the transport parameters earlyParams() carried

    // Deliver at most this many bytes per call, to force a handshake message to be reassembled
    // from pieces. Zero delivers whatever is pending in one go.
    size_t chunk;
} QPeer;

static bool qSend(void* ctx, TlsQuicLevel level, const uint8* data, size_t len)
{
    QPeer* p = (QPeer*)ctx;

    size_t have = p->out[level] ? p->out[level]->len : 0;
    bufResize(&p->out[level], have + len);
    memcpy(p->out[level]->data + have, data, len);
    p->out[level]->len = have + len;

    return true;
}

static bool qSecrets(void* ctx, TlsQuicLevel level, uint16 suite, const uint8* rd, const uint8* wr,
                     size_t len)
{
    QPeer* p = (QPeer*)ctx;

    if (len > TLS13_MAX_HASH)
        return false;

    p->suite     = suite;
    p->secretLen = len;

    if (rd) {
        memcpy(p->rd[level], rd, len);
        p->haveRd[level] = true;
    }
    if (wr) {
        memcpy(p->wr[level], wr, len);
        p->haveWr[level] = true;
    }

    return true;
}

static bool qTransportParams(void* ctx, const uint8* data, size_t len)
{
    QPeer* p = (QPeer*)ctx;

    bufDestroy(&p->peerTp);
    p->peerTp = bufCreate(len ? len : 1);
    if (len)
        memcpy(p->peerTp->data, data, len);
    p->peerTp->len = len;

    return true;
}

static bool qEarlyParams(void* ctx, const uint8* data, size_t len)
{
    QPeer* p = (QPeer*)ctx;

    p->earlyAsked = true;

    bufDestroy(&p->earlyTp);
    p->earlyTp = bufCreate(len ? len : 1);
    if (len)
        memcpy(p->earlyTp->data, data, len);
    p->earlyTp->len = len;

    return !p->earlyRefuse;
}

static void qEarlyData(void* ctx, bool accepted)
{
    QPeer* p         = (QPeer*)ctx;
    p->earlyDecided  = true;
    p->earlyAccepted = accepted;
}

static void qComplete(void* ctx)
{
    ((QPeer*)ctx)->complete = true;
}

static void qAlert(void* ctx, uint8 alert)
{
    QPeer* p = (QPeer*)ctx;
    p->failed = true;
    p->alert  = alert;
}

static const TlsQuicHandlers qHandlers = {
    .sendCrypto      = qSend,
    .secrets         = qSecrets,
    .transportParams = qTransportParams,
    .earlyParams     = qEarlyParams,
    .earlyData       = qEarlyData,
    .complete        = qComplete,
    .alert           = qAlert,
};

static const uint8 clientTp[] = { 0x01, 0x02, 0x03, 0x04 };
static const uint8 serverTp[] = { 0x0a, 0x0b };

static void qPeerDestroy(_Inout_ QPeer* p)
{
    objRelease(&p->tq);
    for (int i = 0; i < 4; i++)
        bufDestroy(&p->out[i]);
    bufDestroy(&p->peerTp);
    bufDestroy(&p->earlyTp);
}

// Hand everything one end has produced to the other, lowest encryption level first.
static bool qDeliver(_Inout_ QPeer* from, _Inout_ QPeer* to)
{
    bool moved = false;

    for (int lvl = 0; lvl < 4; lvl++) {
        while (from->out[lvl] && from->out[lvl]->len > 0) {
            size_t n = from->out[lvl]->len;
            if (from->chunk && n > from->chunk)
                n = from->chunk;

            moved   = true;
            bool ok = tlsquicRecv(to->tq, (TlsQuicLevel)lvl, from->out[lvl]->data, n);

            memmove(from->out[lvl]->data, from->out[lvl]->data + n, from->out[lvl]->len - n);
            from->out[lvl]->len -= n;

            if (!ok)
                return moved;
        }
    }

    return moved;
}

// Run until neither end has anything left to send. The iteration cap is a safety net: a handshake
// that has not settled after this many exchanges is looping.
static void qRun(_Inout_ QPeer* cli, _Inout_ QPeer* srv)
{
    for (int i = 0; i < 32; i++) {
        bool moved = qDeliver(cli, srv);
        moved |= qDeliver(srv, cli);

        if (!moved || cli->failed || srv->failed)
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------

typedef struct QFix {
    TlsTestPKI pki;
    TlsCAStore* ca;
    TlsCAStore* wrong;
    TlsCreds* serverCreds;
    TlsCreds* clientCreds;
    TlsConfig* ccfg;
    TlsConfig* scfg;
    QPeer cli;
    QPeer srv;
} QFix;

static bool qFixInit(_Out_ QFix* f)
{
    memset(f, 0, sizeof(*f));

    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert))
        return false;

    f->wrong = tlscastoreCreate();
    if (!f->wrong || !tlscastoreAddPEM(f->wrong, f->pki.otherCACert))
        return false;

    f->serverCreds = tlscredsCreatePEM(f->pki.serverCert, f->pki.serverKey, NULL);
    f->clientCreds = tlscredsCreatePEM(f->pki.altCert, f->pki.altKey, NULL);
    if (!f->serverCreds || !f->clientCreds)
        return false;

    f->ccfg = tlsconfigCreateClient();
    f->scfg = tlsconfigCreateServer(f->serverCreds);
    if (!f->ccfg || !f->scfg)
        return false;

    tlsconfigSetCA(f->ccfg, f->ca);
    return true;
}

// Give both ends the same two-protocol list, so ALPN actually has something to negotiate.
static bool qFixALPN(_Inout_ QFix* f)
{
    sa_string protos;
    saInit(&protos, string, 2);
    saPush(&protos, string, _S"h3");
    saPush(&protos, string, _S"hq-interop");

    bool ok = tlsconfigSetALPN(f->ccfg, &protos) && tlsconfigSetALPN(f->scfg, &protos);
    saDestroy(&protos);

    return ok;
}

static bool qFixPeers(_Inout_ QFix* f, _In_opt_ strref hostname)
{
    f->cli.tq = tlsquicCreateClient(f->ccfg, hostname);
    f->srv.tq = tlsquicCreateServer(f->scfg);
    if (!f->cli.tq || !f->srv.tq)
        return false;

    tlsquicSetHandlers(f->cli.tq, &qHandlers, &f->cli);
    tlsquicSetHandlers(f->srv.tq, &qHandlers, &f->srv);

    return tlsquicSetTransportParams(f->cli.tq, clientTp, sizeof(clientTp)) &&
           tlsquicSetTransportParams(f->srv.tq, serverTp, sizeof(serverTp));
}

static void qFixDestroy(_Inout_ QFix* f)
{
    qPeerDestroy(&f->cli);
    qPeerDestroy(&f->srv);

    objRelease(&f->ccfg);
    objRelease(&f->scfg);
    objRelease(&f->serverCreds);
    objRelease(&f->clientCreds);
    objRelease(&f->ca);
    objRelease(&f->wrong);

    tlsTestPKIDestroy(&f->pki);
}

// Both ends agreed on the same secrets: what one writes at a level is what the other reads there.
static bool qSecretsAgree(_In_ const QPeer* cli, _In_ const QPeer* srv, TlsQuicLevel level)
{
    return cli->haveWr[level] && srv->haveRd[level] && cli->haveRd[level] && srv->haveWr[level] &&
           cli->secretLen == srv->secretLen &&
           memcmp(cli->wr[level], srv->rd[level], cli->secretLen) == 0 &&
           memcmp(cli->rd[level], srv->wr[level], cli->secretLen) == 0;
}

// ---------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------

// A whole handshake, checked from both ends: secrets agree, transport parameters crossed over,
// ALPN was negotiated, and the client verified the server's certificate against the trust store.
int test_tlsquictest_handshake(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("ALPN setup", qFixALPN(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));

    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("client failed", !f.cli.failed);
    CHECK("server failed", !f.srv.failed);
    CHECK("client did not complete", tlsquicComplete(f.cli.tq));
    CHECK("server did not complete", tlsquicComplete(f.srv.tq));

    CHECK("handshake secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_Handshake));
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));
    CHECK("Initial secrets were reported", !f.cli.haveWr[TLSQL_Initial] &&
                                               !f.srv.haveWr[TLSQL_Initial]);
    CHECK_U("negotiated suite", f.cli.suite, TLS13_AES_128_GCM_SHA256);
    CHECK_U("secret length", f.cli.secretLen, 32);

    CHECK("server did not receive the client's transport parameters",
          f.srv.peerTp && f.srv.peerTp->len == sizeof(clientTp) &&
              memcmp(f.srv.peerTp->data, clientTp, sizeof(clientTp)) == 0);
    CHECK("client did not receive the server's transport parameters",
          f.cli.peerTp && f.cli.peerTp->len == sizeof(serverTp) &&
              memcmp(f.cli.peerTp->data, serverTp, sizeof(serverTp)) == 0);

    string alpn = 0;
    bool gotAlpn = tlsquicGetALPN(f.cli.tq, &alpn) && strEq(alpn, _S"h3");
    strDestroy(&alpn);
    CHECK("client ALPN", gotAlpn);

    gotAlpn = tlsquicGetALPN(f.srv.tq, &alpn) && strEq(alpn, _S"h3");
    strDestroy(&alpn);
    CHECK("server ALPN", gotAlpn);

    TlsInfo info;
    CHECK("client info", tlsquicGetInfo(f.cli.tq, &info));
    bool infoOk = info.secured && info.peerVerified && info.verifyFlags == 0 &&
                  strEq(info.version, _S"TLSv1.3") &&
                  strEq(info.ciphersuite, _S"TLS_AES_128_GCM_SHA256") &&
                  !strEmpty(info.peerSubject);
    nettlsInfoDestroy(&info);
    CHECK("client info contents", infoOk);

    // The server asked for nothing, so it has no peer certificate to report.
    CHECK("server info", tlsquicGetInfo(f.srv.tq, &info));
    infoOk = info.secured && !info.peerVerified && strEmpty(info.peerSubject);
    nettlsInfoDestroy(&info);
    CHECK("server info contents", infoOk);

out:
    qFixDestroy(&f);
    return ret;
}

// The same handshake with every flight delivered a byte at a time, which forces every handshake
// message to be reassembled from pieces rather than arriving whole.
int test_tlsquictest_reassembly(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));

    f.cli.chunk = 1;
    f.srv.chunk = 1;

    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("client failed", !f.cli.failed);
    CHECK("server failed", !f.srv.failed);
    CHECK("client did not complete", tlsquicComplete(f.cli.tq));
    CHECK("server did not complete", tlsquicComplete(f.srv.tq));
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

// A server that only accepts secp256r1, against a client that offers an X25519 key share: the
// server has to ask for a retry, and the second ClientHello has to carry the group it named.
int test_tlsquictest_retry(void)
{
    int ret = 0;
    QFix f;
    static const uint16 p256[] = { TLS13_GROUP_SECP256R1 };

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("server groups", tlsquicSetGroups(f.srv.tq, p256, 1));

    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("client failed", !f.cli.failed);
    CHECK("server failed", !f.srv.failed);
    CHECK("client did not complete", tlsquicComplete(f.cli.tq));
    CHECK("server did not complete", tlsquicComplete(f.srv.tq));
    CHECK("handshake secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_Handshake));
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

// Each cipher suite in turn, which is the only way the SHA-384 key schedule gets exercised: every
// secret, key and Finished value is 48 bytes rather than 32 on that path.
static int quicSuiteRun(uint16 id, size_t secretLen)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("server suites", tlsquicSetSuites(f.srv.tq, &id, 1));

    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("client failed", !f.cli.failed);
    CHECK("server failed", !f.srv.failed);
    CHECK("client did not complete", tlsquicComplete(f.cli.tq));
    CHECK_U("negotiated suite", f.cli.suite, id);
    CHECK_U("secret length", f.cli.secretLen, secretLen);
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

int test_tlsquictest_suite_aes256(void)
{
    return quicSuiteRun(TLS13_AES_256_GCM_SHA384, 48);
}

int test_tlsquictest_suite_chacha(void)
{
    return quicSuiteRun(TLS13_CHACHA20_POLY1305_SHA256, 32);
}

// Mutual authentication: the server requires a client certificate, so it sends a
// CertificateRequest and the client answers with its own Certificate and CertificateVerify.
int test_tlsquictest_mutual(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetCA(f.scfg, f.ca);
    tlsconfigSetAuthMode(f.scfg, TLSAUTH_Required);
    tlsconfigSetCreds(f.ccfg, f.clientCreds);

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("client failed", !f.cli.failed);
    CHECK("server failed", !f.srv.failed);
    CHECK("server did not complete", tlsquicComplete(f.srv.tq));

    TlsInfo info;
    CHECK("server info", tlsquicGetInfo(f.srv.tq, &info));
    bool infoOk = info.peerVerified && info.verifyFlags == 0 && !strEmpty(info.peerSubject);
    nettlsInfoDestroy(&info);
    CHECK("the server did not verify a client certificate", infoOk);

out:
    qFixDestroy(&f);
    return ret;
}

// The same, with the client holding no identity: the empty Certificate it sends instead is not
// enough for a server that requires one.
int test_tlsquictest_mutual_missing(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetCA(f.scfg, f.ca);
    tlsconfigSetAuthMode(f.scfg, TLSAUTH_Required);

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("the server accepted a client with no certificate", !tlsquicComplete(f.srv.tq));
    CHECK_U("server alert", tlsquicAlert(f.srv.tq), TLS13_ALERT_CERTIFICATE_REQUIRED);

out:
    qFixDestroy(&f);
    return ret;
}

// A client whose trust store did not issue the server's certificate must not complete.
int test_tlsquictest_untrusted(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    tlsconfigSetCA(f.ccfg, f.wrong);

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("the client trusted an unknown certificate authority", !tlsquicComplete(f.cli.tq));
    CHECK_U("client alert", tlsquicAlert(f.cli.tq), TLS13_ALERT_BAD_CERTIFICATE);

out:
    qFixDestroy(&f);
    return ret;
}

// The certificate verifies, but not for the name the client asked for.
int test_tlsquictest_wrongname(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S"not-the-server.invalid"));

    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("the client accepted a certificate for another name", !tlsquicComplete(f.cli.tq));
    CHECK_U("client alert", tlsquicAlert(f.cli.tq), TLS13_ALERT_BAD_CERTIFICATE);

out:
    qFixDestroy(&f);
    return ret;
}

// No application protocol in common, which QUIC treats as a fatal condition rather than as an
// absent extension.
int test_tlsquictest_alpn_mismatch(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    sa_string cp, sp;
    saInit(&cp, string, 1);
    saPush(&cp, string, _S"h3");
    saInit(&sp, string, 1);
    saPush(&sp, string, _S"myproto");

    bool alpnOk = tlsconfigSetALPN(f.ccfg, &cp) && tlsconfigSetALPN(f.scfg, &sp);
    saDestroy(&cp);
    saDestroy(&sp);
    CHECK("ALPN setup", alpnOk);

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("the server accepted a protocol it does not speak", !tlsquicComplete(f.srv.tq));
    CHECK_U("server alert", tlsquicAlert(f.srv.tq), TLS13_ALERT_NO_APPLICATION_PROTOCOL);

out:
    qFixDestroy(&f);
    return ret;
}

// Corrupting the server's last handshake flight has to be caught by the Finished check, which is
// the only thing standing between a downgraded handshake and a completed one.
int test_tlsquictest_tampered(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));

    CHECK("client start", tlsquicStart(f.cli.tq));

    // Let the server produce its whole flight, then flip the last byte of it -- the final byte of
    // the Finished verify_data.
    qDeliver(&f.cli, &f.srv);
    CHECK("the server sent no handshake flight",
          f.srv.out[TLSQL_Handshake] && f.srv.out[TLSQL_Handshake]->len > 0);

    Buffer flight = f.srv.out[TLSQL_Handshake];
    flight->data[flight->len - 1] ^= 0x01;

    qRun(&f.cli, &f.srv);

    CHECK("the client accepted a corrupted Finished", !tlsquicComplete(f.cli.tq));
    CHECK_U("client alert", tlsquicAlert(f.cli.tq), TLS13_ALERT_DECRYPT_ERROR);

out:
    qFixDestroy(&f);
    return ret;
}

// Resumption: the first handshake leaves a ticket in the client config's cache, and the second one
// offers it back. A resumed handshake sends no certificate, so the server never signs anything and
// the client never verifies a chain.
int test_tlsquictest_resume(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetResumption(f.ccfg, true, timeS(600));
    tlsconfigSetResumption(f.scfg, true, timeS(600));

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("first handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));

    // Second connection, same configurations, so the ticket the first one produced is offered.
    qPeerDestroy(&f.cli);
    qPeerDestroy(&f.srv);
    memset(&f.cli, 0, sizeof(f.cli));
    memset(&f.srv, 0, sizeof(f.srv));

    CHECK("second peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("second client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("second handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

    // No certificate changes hands on a resumed handshake, which is what tells it apart from a
    // second full one.
    TlsInfo info;
    CHECK("client info", tlsquicGetInfo(f.cli.tq, &info));
    bool resumed = !info.peerVerified && strEmpty(info.peerSubject);
    nettlsInfoDestroy(&info);
    CHECK("the second handshake was not resumed", resumed);

out:
    qFixDestroy(&f);
    return ret;
}

// 0-RTT at the handshake level: the second connection derives a secret for TLSQL_EarlyData before
// it has heard anything back, and the server arrives at the same one from the ClientHello alone.
// The two secrets matching is the whole mechanism -- there is nothing else keying them together.
int test_tlsquictest_early(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetResumption(f.ccfg, true, timeS(600));
    tlsconfigSetResumption(f.scfg, true, timeS(600));
    CHECK("client early data", tlsconfigSetEarlyData(f.ccfg, true));
    CHECK("server early data", tlsconfigSetEarlyData(f.scfg, true));

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);
    CHECK("first handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));

    // Nothing to resume the first time round, so nothing early happened.
    CHECK("early data on a fresh handshake", !f.cli.earlyDecided && !f.srv.earlyDecided);

    qPeerDestroy(&f.cli);
    qPeerDestroy(&f.srv);
    memset(&f.cli, 0, sizeof(f.cli));
    memset(&f.srv, 0, sizeof(f.srv));

    CHECK("second peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("second client start", tlsquicStart(f.cli.tq));

    // Before a single byte has come back: the transport was asked about the old limits and given
    // a key to write under.
    CHECK("client was not asked about the old limits", f.cli.earlyAsked);
    CHECK("the old limits are the ones the server sent",
          f.cli.earlyTp && f.cli.earlyTp->len == sizeof(serverTp) &&
              memcmp(f.cli.earlyTp->data, serverTp, sizeof(serverTp)) == 0);
    CHECK("client has no 0-RTT write key", f.cli.haveWr[TLSQL_EarlyData]);
    CHECK("0-RTT is one directional", !f.cli.haveRd[TLSQL_EarlyData]);

    qRun(&f.cli, &f.srv);

    CHECK("second handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));
    CHECK("server was not asked about the old limits", f.srv.earlyAsked);
    CHECK("server took the early data", f.srv.earlyDecided && f.srv.earlyAccepted);
    CHECK("client was told so", f.cli.earlyDecided && f.cli.earlyAccepted);

    CHECK("server has no 0-RTT read key", f.srv.haveRd[TLSQL_EarlyData]);
    CHECK("the server writes no 0-RTT", !f.srv.haveWr[TLSQL_EarlyData]);
    CHECK("0-RTT secrets disagree",
          f.cli.secretLen == f.srv.secretLen &&
              memcmp(f.cli.wr[TLSQL_EarlyData], f.srv.rd[TLSQL_EarlyData], f.cli.secretLen) == 0);

    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

// A server whose transport says the old limits are no longer on offer refuses the early data. The
// handshake is not affected by that -- it resumes and completes as it would have -- which is what
// makes the refusal safe to take.
int test_tlsquictest_early_refused(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetResumption(f.ccfg, true, timeS(600));
    tlsconfigSetResumption(f.scfg, true, timeS(600));
    CHECK("client early data", tlsconfigSetEarlyData(f.ccfg, true));
    CHECK("server early data", tlsconfigSetEarlyData(f.scfg, true));

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);
    CHECK("first handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));

    qPeerDestroy(&f.cli);
    qPeerDestroy(&f.srv);
    memset(&f.cli, 0, sizeof(f.cli));
    memset(&f.srv, 0, sizeof(f.srv));

    CHECK("second peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    f.srv.earlyRefuse = true;

    CHECK("second client start", tlsquicStart(f.cli.tq));
    CHECK("client armed 0-RTT", f.cli.haveWr[TLSQL_EarlyData]);

    qRun(&f.cli, &f.srv);

    CHECK("second handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));
    CHECK("the server was asked and said no", f.srv.earlyAsked);
    CHECK("server read early data anyway", !f.srv.haveRd[TLSQL_EarlyData]);
    CHECK("client was not told it was refused", f.cli.earlyDecided && !f.cli.earlyAccepted);
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

// A HelloRetryRequest replaces the ClientHello the 0-RTT keys were derived from, so whatever was
// sent under them can never be read and the second hello may not ask again. Both ends have to
// reach that conclusion on their own -- the client because it built the keys, the server because
// it would otherwise be deriving a read key for a transcript that no longer exists.
int test_tlsquictest_early_hrr(void)
{
    int ret = 0;
    QFix f;
    static const uint16 p256[] = { TLS13_GROUP_SECP256R1 };

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetResumption(f.ccfg, true, timeS(600));
    tlsconfigSetResumption(f.scfg, true, timeS(600));
    CHECK("client early data", tlsconfigSetEarlyData(f.ccfg, true));
    CHECK("server early data", tlsconfigSetEarlyData(f.scfg, true));

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);
    CHECK("first handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));

    qPeerDestroy(&f.cli);
    qPeerDestroy(&f.srv);
    memset(&f.cli, 0, sizeof(f.cli));
    memset(&f.srv, 0, sizeof(f.srv));

    CHECK("second peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));

    // The client offers a key share the server will not take, which is the whole of what makes a
    // HelloRetryRequest happen.
    CHECK("server groups", tlsquicSetGroups(f.srv.tq, p256, 1));

    CHECK("second client start", tlsquicStart(f.cli.tq));
    CHECK("client armed 0-RTT", f.cli.haveWr[TLSQL_EarlyData]);

    qRun(&f.cli, &f.srv);

    CHECK("second handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));
    CHECK("client still thinks its early data is live",
          f.cli.earlyDecided && !f.cli.earlyAccepted);
    CHECK("server derived a 0-RTT read key after a retry", !f.srv.haveRd[TLSQL_EarlyData]);
    CHECK("server took early data it cannot have keys for",
          !f.srv.earlyDecided || !f.srv.earlyAccepted);
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

// RFC 9001 section 8.3: QUIC marks the end of early data by moving to 1-RTT keys, so the TLS
// message that would say so has nothing to do and must never appear. One arriving is a peer
// speaking TLS over TCP at a QUIC endpoint, and the handshake ends there.
int test_tlsquictest_early_eoed(void)
{
    int ret = 0;
    QFix f;
    static const uint8 eoed[] = { 5, 0, 0, 0 };   // EndOfEarlyData, empty body

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);
    CHECK("handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));

    CHECK("EndOfEarlyData was accepted",
          !tlsquicRecv(f.srv.tq, TLSQL_EarlyData, eoed, sizeof(eoed)));
    CHECK("no alert was raised", f.srv.failed);
    CHECK_U("wrong alert", f.srv.alert, TLS13_ALERT_UNEXPECTED_MESSAGE);

out:
    qFixDestroy(&f);
    return ret;
}

// A ticket sealed under one server configuration is meaningless to another, and the handshake has
// to fall back to a full one rather than fail.
int test_tlsquictest_resume_rejected(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetResumption(f.ccfg, true, timeS(600));
    tlsconfigSetResumption(f.scfg, true, timeS(600));

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);
    CHECK("first handshake failed", tlsquicComplete(f.cli.tq));

    // A fresh server configuration has a fresh ticket key, so the offered ticket cannot open.
    objRelease(&f.scfg);
    f.scfg = tlsconfigCreateServer(f.serverCreds);
    CHECK("second server config", f.scfg != NULL);
    tlsconfigSetResumption(f.scfg, true, timeS(600));

    qPeerDestroy(&f.cli);
    qPeerDestroy(&f.srv);
    memset(&f.cli, 0, sizeof(f.cli));
    memset(&f.srv, 0, sizeof(f.srv));

    CHECK("second peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("second client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);

    CHECK("second handshake failed", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));

    TlsInfo info;
    CHECK("client info", tlsquicGetInfo(f.cli.tq, &info));
    bool full = info.peerVerified && !strEmpty(info.peerSubject);
    nettlsInfoDestroy(&info);
    CHECK("a ticket from another server was accepted", full);

out:
    qFixDestroy(&f);
    return ret;
}

// A resumed handshake proves possession of the ticket with a binder over the ClientHello. Flip a
// bit of it and the server has to refuse, since that is the only thing making a stolen ticket
// identity useless on its own.
int test_tlsquictest_resume_badbinder(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    tlsconfigSetResumption(f.ccfg, true, timeS(600));
    tlsconfigSetResumption(f.scfg, true, timeS(600));

    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("client start", tlsquicStart(f.cli.tq));
    qRun(&f.cli, &f.srv);
    CHECK("first handshake failed", tlsquicComplete(f.cli.tq));

    qPeerDestroy(&f.cli);
    qPeerDestroy(&f.srv);
    memset(&f.cli, 0, sizeof(f.cli));
    memset(&f.srv, 0, sizeof(f.srv));

    CHECK("second peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));
    CHECK("second client start", tlsquicStart(f.cli.tq));

    // The binder is the last thing in a ClientHello that offers a pre-shared key.
    Buffer ch = f.cli.out[TLSQL_Initial];
    CHECK("no ClientHello was produced", ch && ch->len > 0);
    ch->data[ch->len - 1] ^= 0x01;

    qRun(&f.cli, &f.srv);

    CHECK("the server accepted a corrupted PSK binder", !tlsquicComplete(f.srv.tq));
    CHECK_U("server alert", tlsquicAlert(f.srv.tq), TLS13_ALERT_DECRYPT_ERROR);

out:
    qFixDestroy(&f);
    return ret;
}

// A server must not be able to read 1-RTT packets before it has checked the client's Finished, so
// it publishes its application write secret with its own flight and holds the read secret back.
int test_tlsquictest_key_order(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));

    CHECK("client start", tlsquicStart(f.cli.tq));

    // One delivery: the server has now seen the ClientHello and sent its whole flight, and is
    // waiting on the client's Finished.
    qDeliver(&f.cli, &f.srv);

    CHECK("the server did not publish its application write secret", f.srv.haveWr[TLSQL_App]);
    CHECK("the server published its application read secret too early",
          !f.srv.haveRd[TLSQL_App]);
    CHECK("the server published handshake secrets late",
          f.srv.haveRd[TLSQL_Handshake] && f.srv.haveWr[TLSQL_Handshake]);

    // Which label each direction gets is the one thing two cx engines talking to each other cannot
    // disagree about, so it is pinned here against the labels RFC 8446 names. The transcript is
    // still standing exactly where the application secrets were derived from it, so they can be
    // recomputed and compared.
    Tls13Hs* shs = &f.srv.tq->st->hs;
    uint8 thash[TLS13_MAX_HASH];
    uint8 expect[TLS13_MAX_HASH];
    uint8 clientAp[TLS13_MAX_HASH];
    size_t hlen = shs->suite->hashLen;

    CHECK("transcript hash", _tls13TranscriptHash(&shs->tr, thash));

    CHECK("derive s ap traffic",
          _tls13SchedDerive(&shs->sched, _S"s ap traffic", thash, hlen, expect));
    CHECK("the server writes with something other than s ap traffic",
          memcmp(expect, f.srv.wr[TLSQL_App], hlen) == 0);

    CHECK("derive c ap traffic",
          _tls13SchedDerive(&shs->sched, _S"c ap traffic", thash, hlen, clientAp));
    CHECK("the server holds something other than c ap traffic for the client",
          memcmp(clientAp, shs->clientApSecret, hlen) == 0);

    // The same for the handshake level, one step down: the secret the server writes with is the
    // one it derived under the server label, and the one it reads with is the client's.
    CHECK("the server writes handshake records with the client's secret",
          memcmp(f.srv.wr[TLSQL_Handshake], shs->serverHsSecret, hlen) == 0);
    CHECK("the server reads handshake records with its own secret",
          memcmp(f.srv.rd[TLSQL_Handshake], shs->clientHsSecret, hlen) == 0);

    qRun(&f.cli, &f.srv);

    CHECK("handshake did not complete", tlsquicComplete(f.cli.tq) && tlsquicComplete(f.srv.tq));
    CHECK("the server never published its application read secret", f.srv.haveRd[TLSQL_App]);
    CHECK("the server reads with something other than c ap traffic",
          memcmp(clientAp, f.srv.rd[TLSQL_App], hlen) == 0);
    CHECK("application secrets disagree", qSecretsAgree(&f.cli, &f.srv, TLSQL_App));

out:
    qFixDestroy(&f);
    return ret;
}

// A hand-built ClientHello, so that the server's own checks can be exercised against hellos the cx
// client would never send.
typedef struct HelloOpts {
    bool sessionId;     // ask for TLS compatibility mode, which QUIC forbids
    bool omitTp;        // leave out quic_transport_parameters
    bool omitSigAlgs;   // leave out signature_algorithms
} HelloOpts;

static _Ret_maybenull_ Buffer buildHello(_In_ const HelloOpts* o)
{
    static const uint8 versions[] = { 0x02, 0x03, 0x04 };
    static const uint8 groups[]   = { 0x00, 0x02, 0x00, 0x1d };
    static const uint8 sigalgs[]  = { 0x00, 0x02, 0x04, 0x03 };
    static const uint8 tp[]       = { 0x01, 0x02 };

    // A real X25519 key share, so that a server which fails to refuse one of these hellos goes on
    // to complete its flight rather than tripping over a bad key exchange -- which would look the
    // same as the refusal being tested.
    uint8 keyshare[6 + 32] = { 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20 };

    Tls13Kex kex;
    size_t publen;
    if (!_tls13KexGenerate(&kex, TLS13_GROUP_X25519))
        return NULL;

    bool kexOk = _tls13KexPublic(&kex, keyshare + 6, sizeof(keyshare) - 6, &publen) && publen == 32;
    _tls13KexDestroy(&kex);

    if (!kexOk)
        return NULL;

    Tls13Hello h;
    memset(&h, 0, sizeof(h));

    h.legacyVersion = 0x0303;
    h.sessionIdLen  = o->sessionId ? 32 : 0;
    h.suites[0]     = TLS13_AES_128_GCM_SHA256;
    h.nsuites       = 1;

    _tls13HelloAddExt(&h, TLS13_EXT_SUPPORTED_VERSIONS, versions, sizeof(versions));
    _tls13HelloAddExt(&h, TLS13_EXT_SUPPORTED_GROUPS, groups, sizeof(groups));
    if (!o->omitSigAlgs)
        _tls13HelloAddExt(&h, TLS13_EXT_SIGNATURE_ALGORITHMS, sigalgs, sizeof(sigalgs));
    _tls13HelloAddExt(&h, TLS13_EXT_KEY_SHARE, keyshare, sizeof(keyshare));
    if (!o->omitTp)
        _tls13HelloAddExt(&h, TLS13_EXT_QUIC_TRANSPORT_PARAMS, tp, sizeof(tp));

    return _tls13HelloEncode(&h, false);
}

static int helloReject(_In_ const HelloOpts* o, uint8 wantAlert)
{
    int ret = 0;
    QFix f;
    Buffer ch = NULL;

    CHECK("fixture setup", qFixInit(&f));
    CHECK("peer setup", qFixPeers(&f, _S TLS_TEST_HOSTNAME));

    ch = buildHello(o);
    CHECK("could not build a ClientHello", ch != NULL);

    tlsquicRecv(f.srv.tq, TLSQL_Initial, ch->data, ch->len);

    CHECK("the server accepted a ClientHello it should have refused",
          !tlsquicComplete(f.srv.tq));
    CHECK_U("server alert", tlsquicAlert(f.srv.tq), wantAlert);

out:
    bufDestroy(&ch);
    qFixDestroy(&f);
    return ret;
}

// RFC 9001 section 8.4 bans the TLS 1.3 middlebox compatibility mode over QUIC, which a client
// asks for by putting something in legacy_session_id.
int test_tlsquictest_compat_mode(void)
{
    HelloOpts o = { .sessionId = true };
    return helloReject(&o, TLS13_ALERT_ILLEGAL_PARAMETER);
}

// Transport parameters are mandatory in both directions.
int test_tlsquictest_no_transport_params(void)
{
    HelloOpts o = { .omitTp = true };
    return helloReject(&o, TLS13_ALERT_MISSING_EXTENSION);
}

// So is signature_algorithms, without which the server cannot choose how to sign.
int test_tlsquictest_no_sigalgs(void)
{
    HelloOpts o = { .omitSigAlgs = true };
    return helloReject(&o, TLS13_ALERT_MISSING_EXTENSION);
}

// The bytes a CertificateVerify signature actually covers, checked against RFC 8446 section 4.4.3
// rather than against the engine's own idea of them: 64 spaces, the context string for whichever
// end signed, a zero byte, and the transcript hash.
int test_tlsquictest_certverify_content(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture setup", qFixInit(&f));

    uint8 thash[32];
    for (int i = 0; i < 32; i++)
        thash[i] = (uint8)(0xa0 + i);

    uint8 sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t sigLen;
    CHECK("signing failed",
          _tls13SigMake(TLS13_SIG_ECDSA_SECP256R1_SHA256, &f.serverCreds->st->key, true, thash,
                        sizeof(thash), sig, sizeof(sig), &sigLen));

    static const char srvctx[] = "TLS 1.3, server CertificateVerify";
    const size_t ctxLen        = sizeof(srvctx) - 1;

    uint8 content[64 + sizeof(srvctx) + 32];
    memset(content, 0x20, 64);
    memcpy(content + 64, srvctx, ctxLen);
    content[64 + ctxLen] = 0;
    memcpy(content + 64 + ctxLen + 1, thash, sizeof(thash));

    uint8 hash[32];
    size_t hashLen;
    CHECK("hashing failed",
          psa_hash_compute(PSA_ALG_SHA_256, content, sizeof(content), hash, sizeof(hash),
                           &hashLen) == PSA_SUCCESS);

    CHECK("the signature does not cover the bytes RFC 8446 specifies",
          mbedtls_pk_verify(&f.serverCreds->st->cert.pk, MBEDTLS_MD_SHA256, hash, hashLen, sig,
                            sigLen) == 0);

    // The same signature must not pass as a client's, which is the only thing keeping the two
    // directions apart.
    CHECK("a server signature verified as a client signature",
          !_tls13SigCheck(TLS13_SIG_ECDSA_SECP256R1_SHA256, &f.serverCreds->st->cert.pk, false,
                          thash, sizeof(thash), sig, sigLen));

    CHECK("a server signature did not verify as one",
          _tls13SigCheck(TLS13_SIG_ECDSA_SECP256R1_SHA256, &f.serverCreds->st->cert.pk, true,
                         thash, sizeof(thash), sig, sigLen));

out:
    qFixDestroy(&f);
    return ret;
}

// The engine refuses the setups that cannot work, rather than failing somewhere inside a
// handshake where the cause is much harder to see.
int test_tlsquictest_misuse(void)
{
    int ret = 0;
    QFix f;
    TlsQuic* tq = NULL;
    static const uint16 badGroup[] = { 0x1234 };
    static const uint16 badSuite[] = { 0x1301, 0x00ff };

    CHECK("fixture setup", qFixInit(&f));

    CHECK("a client handshake was built on a server configuration",
          tlsquicCreateClient(f.scfg, _S TLS_TEST_HOSTNAME) == NULL);
    CHECK("a server handshake was built on a client configuration",
          tlsquicCreateServer(f.ccfg) == NULL);

    tq = tlsquicCreateClient(f.ccfg, _S TLS_TEST_HOSTNAME);
    CHECK("client handshake", tq != NULL);
    tlsquicSetHandlers(tq, &qHandlers, &f.cli);

    CHECK("an unsupported group was accepted", !tlsquicSetGroups(tq, badGroup, 1));
    CHECK("an empty group list was accepted", !tlsquicSetGroups(tq, badGroup, 0));
    CHECK("an unsupported cipher suite was accepted", !tlsquicSetSuites(tq, badSuite, 2));

    // Transport parameters are not optional in QUIC, so a handshake with none refuses to start.
    CHECK("a handshake started without transport parameters", !tlsquicStart(tq));

out:
    objRelease(&tq);
    qFixDestroy(&f);
    return ret;
}

testfunc tlsquictest_funcs[] = {
    { "handshake", test_tlsquictest_handshake },
    { "reassembly", test_tlsquictest_reassembly },
    { "retry", test_tlsquictest_retry },
    { "suite_aes256", test_tlsquictest_suite_aes256 },
    { "suite_chacha", test_tlsquictest_suite_chacha },
    { "mutual", test_tlsquictest_mutual },
    { "mutual_missing", test_tlsquictest_mutual_missing },
    { "untrusted", test_tlsquictest_untrusted },
    { "wrongname", test_tlsquictest_wrongname },
    { "alpn_mismatch", test_tlsquictest_alpn_mismatch },
    { "tampered", test_tlsquictest_tampered },
    { "resume", test_tlsquictest_resume },
    { "resume_rejected", test_tlsquictest_resume_rejected },
    { "resume_badbinder", test_tlsquictest_resume_badbinder },
    { "early", test_tlsquictest_early },
    { "early_refused", test_tlsquictest_early_refused },
    { "early_hrr", test_tlsquictest_early_hrr },
    { "early_eoed", test_tlsquictest_early_eoed },
    { "key_order", test_tlsquictest_key_order },
    { "compat_mode", test_tlsquictest_compat_mode },
    { "no_transport_params", test_tlsquictest_no_transport_params },
    { "no_sigalgs", test_tlsquictest_no_sigalgs },
    { "certverify_content", test_tlsquictest_certverify_content },
    { "misuse", test_tlsquictest_misuse },
    { NULL, NULL },
};
