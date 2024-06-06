#include <tree/merkletree.h>
#include <chk/pkgchk.h>
#include <chk/pkg_helper.h>
#include <utilities/my_utils.h>
#include <peer_2_peer/peer_handler.h>
#include <peer_2_peer/peer_data_sync.h>
#include <peer_2_peer/packet.h>
#include <peer_2_peer/package.h>

void peer_handler(void *args_void) 
{
    pthread_cleanup_push(cleanup_peer_handler, args_void);

    peer_thr_args_t *args = (peer_thr_args_t*) args_void;
    peer_t* peer = args->peer;
    peers_t* peers = args->peers;
    bpkgs_t* bpkgs = args->bpkgs;

    request_q_t *reqs_q = args->requests;
    request_t* req_recent = NULL;


    acp_wait_ack(peer);

    while (true) 
    {
        request_t* req_recent = process_request_shared(peer, reqs_q, peers);   

        pkt_t* pkt = peer_try_receive(peer);
        
        if (pkt != NULL){
            process_pkt_in(peer, pkt, bpkgs, req_recent);
        }
        
        pthread_testcancel();
    }
    pthread_cleanup_pop(1);
}

void peer_cleanup_handler(void *arg) 
{
    peer_thr_args_t *args = (peer_thr_args_t*) arg;
    if (args->peer != NULL) {

        if (args->peer->sock_fd >= 0) {
            close(args->peer->sock_fd);
            args->peer->sock_fd = -1;

            peer_outgoing_requests_destroy(args->peer, args->requests);
            peers_remove(args->peers, args->peer->ip, args->peer->port);
        }
    }
}

void peer_create_thread(peer_t* new_peer, request_q_t* reqs_q, peers_t* peers, bpkgs_t* bpkgs)
{
    peer_thr_args_t* args = my_malloc(sizeof(peer_thr_args_t));

    args->peer = new_peer;
    args->requests = reqs_q;
    args->peers = peers;
    args->bpkgs = bpkgs;

    // Create the thread
    int result = pthread_create(&new_peer->thread, NULL, ((void*) &peer_handler), args);
    if (result != 0) {
        perror("Thread creation failed....");
        free(args); 
        exit(EXIT_FAILURE);
    }
}

pkt_t* peer_try_receive(peer_t* peer) 
{
    if (!peer || peer->sock_fd < 0) {
        fprintf(stderr, "Invalid peer or socket not initialized.\n");
        return NULL;
    }

    struct timeval tv = {3, 0};  
    setsockopt(peer->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t *buffer = malloc(PAYLOAD_MAX);
    if (!buffer) {
        perror("Failed to allocate buffer for receiving pkt");
        return NULL;
    }

    ssize_t received = 0;
    while (received < PAYLOAD_MAX) {
        ssize_t n = recv(peer->sock_fd, buffer + received, PAYLOAD_MAX - received, 0);
        if (n < 0) {
            perror("Receive failed or timed out");
            free(buffer);
            return NULL;
        }
        if (n == 0) {
            fprintf(stderr, "Connection closed by peer.\n");
            free(buffer);
            return NULL;
        }
        received += n;
    }

    pkt_t* pkt = malloc(sizeof(pkt_t));

    if (!pkt) {
        perror("Failed to allocate pkt");
        free(buffer);
        return NULL;
    }
    pkt_unmarshall(pkt, buffer);

    free(buffer);
    return pkt;
}

request_t* peer_process_request_shared(peer_t* peer, request_q_t* reqs_q, peers_t* peers)
{
    pthread_mutex_lock(&reqs_q->lock);
    request_t* req = reqs_nextup(reqs_q);

    if (req != NULL && req->peer->port == peer->port && strcmp(req->peer->ip, peer->ip) == 0)
    {
        reqs_dequeue(reqs_q);
        process_pkt_out(peer, req->pkt);
    }

    pthread_mutex_unlock(&reqs_q->lock);
}

void try_send(peer_t* peer, pkt_t* pkt_out)
{
    if (!peer || peer->sock_fd < 0){
        perror("Cannot send to peer, peer doesn't exist...\n");
        return;
    }
    uint8_t buffer[PAYLOAD_MAX];
    pkt_marshall(pkt_out, buffer);
    ssize_t nsent;

    int total = 0;
    int bytesleft = sizeof(buffer);
    int n;

    while(total < sizeof(buffer)) {

        n = send(peer->sock_fd, buffer + total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    if (total == sizeof(buffer))
    {
        debug_print("successfully sent entire pkt to %d\n", peer->port);
    }

    else if (n<0)
    {
        debug_print("Failed to send pkt.\n");
        return;
    }

    fprintf(stderr, "Sent incomplete pkt to %d\n", peer->port);
}

void send_acp(peer_t* peer)
{
    
    pkt_t* pkt = pkt_create(PKT_MSG_ACP, 0, NULL);
    try_send(peer, pkt);
}

void send_ack(peer_t* peer)
{
    
    pkt_t* pkt = pkt_create(PKT_MSG_ACK, 0, NULL);
    try_send(peer, pkt);
}

void send_res(peer_t* peer, uint8_t err, payload_t* payload)
{
    pkt_t* pkt = pkt_create(PKT_MSG_RES, err , payload);
    try_send(peer, pkt);
}

void send_req(peer_t* peer, pkt_t* payload)
{
    pkt_t* pkt = pkt_create(PKT_MSG_REQ, 0, payload);
    try_send(peer, pkt);
}

void send_png(peer_t* peer)
{
    
    pkt_t* pkt = pkt_create(PKT_MSG_PNG, 0 , NULL);
    try_send(peer, pkt);
}

void send_pog(peer_t* peer)
{
    pkt_t* pkt = pkt_create(PKT_MSG_POG, 0 , NULL);

    try_send(peer, &pkt);
}

void send_dsn(peer_t* peer)
{
    pkt_t* pkt = pkt_create(PKT_MSG_DSN, 0 , NULL);
    try_send(peer, &pkt);
}

int acp_wait_ack(peer_t *peer) {
    
    send_acp(peer);

    pkt_t* pkt = try_recieve(peer);

    if (pkt != NULL && pkt->msg_code == PKT_MSG_ACK)
    {
        return 1;
    }
    return 0;
}

void process_pkt_in(peer_t* peer, pkt_t* pkt_in, bpkg_t* bpkgs, request_t* req_recent)
{
    int err = 0;
    payload_t* payload;
    switch(pkt_in->msg_code)
    {
        case PKT_MSG_PNG:
            send_pog(peer);
            break;

        case PKT_MSG_ACP:
            send_ack(peer);
            break;

        // Request payload recieved:
        case PKT_MSG_REQ:
            //Try to retrieve the package chunk requested by this peer:
            payload = payload_get_res_for_req(pkt_in->payload, bpkgs_mngr);
            if (payload == NULL)
            {
                err = -1;
            }
            //Send requesting peer the result of the search:
            send_res(peer, err, payload);
            debug_print("Sent P[%d] the requested chunk if exists...", peer->port);
            break;

        case PKT_MSG_DSN:
            send_dsn(peer);
            pthread_exit((void* )0);
            break;

        case PKT_MSG_RES:

            if (pkt_install(pkt_in, peer, bpkgs_mngr) < 0) {
                req_recent->status = FAILED;
                debug_print("Failed to install pkt...\n");
                break;
            }

            debug_print("Successfully recieved and installed packet from peer!"\n);
            req_recent->status = SUCCESS;
            pthread_cond_signal(&req_recent->cond);
            break;
   
        default:
            break;
    }
}

void process_pkt_out(peer_t* peer, pkt_t* pkt)
{
    switch(pkt->msg_code){
        case PKT_MSG_PNG:
            send_png(peer);
            break;
        case PKT_MSG_REQ:
            send_req(peer, pkt);
            break;
        case PKT_MSG_DSN:
            send_dsn(peer);
            pthread_cancel(&peer->thread);
            pthread_join(&peer->thread, NULL);
            peer = NULL;
            break;
        default:
            break;
    }
}

request_t* process_request_shared(peer_t* peer, request_q_t* reqs_q, peers_t* peers) {
    pthread_mutex_lock(&reqs_q->lock);
    request_t* req = reqs_nextup(reqs_q);

    if (req != NULL && req->peer->port == peer->port) {
        uint16_t code = req->pkt->msg_code;

        switch (code) {
            case PKT_MSG_PNG:
                send_png(peer);
                break;
            case PKT_MSG_ACP:
                send_acp(peer);
                break;
            case PKT_MSG_REQ:
                try_send(peer, req->pkt);
                break;
            case PKT_MSG_DSN:
                send_dsn(peer);
                break;
        }

        reqs_dequeue(reqs_q);
    }

    pthread_mutex_unlock(&reqs_q->lock);
    return req;
}