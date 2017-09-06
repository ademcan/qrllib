/*
xmss_fast.c version 20160722
Andreas Hülsing
Joost Rijneveld
Public domain.
*/

#include "algsxmss_fast.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <math.h>
#include "fips202.h"
#include "randombytes.h"
#include "wots.h"
#include "hash.h"
#include "xmss_commons.h"
#include "hash_address.h"
#include <stdio.h>
#include <fips202.h>

xmssfast_params paramsfast;

/**
 * Used for pseudorandom keygeneration,
 * generates the seed for the WOTS keypair at address addr
 *
 * takes n byte sk_seed and returns n byte seed using 32 byte address addr.
 */
static void get_seed(unsigned char *seed, const unsigned char *sk_seed, int n, uint32_t addr[8])
{
  unsigned char bytes[32];
  // Make sure that chain addr, hash addr, and key bit are 0!
  setChainADRS(addr,0);
  setHashADRS(addr,0);
  setKeyAndMask(addr,0);
  // Generate pseudorandom value
  addr_to_byte(bytes, addr);
  prf(seed, bytes, sk_seed, n);
}

/**
 * Initialize xmss params struct
 * parameter names are the same as in the draft
 * parameter k is K as used in the BDS algorithm
 */
int xmssfast_set_params(xmssfast_params *params, int n, int h, int w, int k)
{
  if (k >= h || k < 2 || (h - k) % 2) {
    fprintf(stderr, "For BDS traversal, H - K must be even, with H > K >= 2!\n");
    return 1;
  }
  params->h = h;
  params->n = n;
  params->k = k;
  wots_params wots_par;
  wots_set_params(&wots_par, n, w);
  params->wots_par = wots_par;
  return 0;
}

/**
 * Initialize BDS state struct
 * parameter names are the same as used in the description of the BDS traversal
 */
void xmss_set_bds_state(bds_state *state, unsigned char *stack, int stackoffset, unsigned char *stacklevels, unsigned char *auth, unsigned char *keep, treehash_inst *treehash, unsigned char *retain, int next_leaf)
{
  state->stack = stack;
  state->stackoffset = stackoffset;
  state->stacklevels = stacklevels;
  state->auth = auth;
  state->keep = keep;
  state->treehash = treehash;
  state->retain = retain;
  state->next_leaf = next_leaf;
}

/**
 * Computes a leaf from a WOTS public key using an L-tree.
 */
static void l_tree(unsigned char *leaf, unsigned char *wots_pk, const xmssfast_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  unsigned int l = params->wots_par.len;
  unsigned int n = params->n;
  uint32_t i = 0;
  uint32_t height = 0;
  uint32_t bound;

  //ADRS.setTreeHeight(0);
  setTreeHeight(addr, height);
  
  while (l > 1) {
     bound = l >> 1; //floor(l / 2);
     for (i = 0; i < bound; i++) {
       //ADRS.setTreeIndex(i);
       setTreeIndex(addr, i);
       //wots_pk[i] = RAND_HASH(pk[2i], pk[2i + 1], SEED, ADRS);
       hash_h(wots_pk+i*n, wots_pk+i*2*n, pub_seed, addr, n);
     }
     //if ( l % 2 == 1 ) {
     if (l & 1) {
       //pk[floor(l / 2) + 1] = pk[l];
       memcpy(wots_pk+(l>>1)*n, wots_pk+(l-1)*n, n);
       //l = ceil(l / 2);
       l=(l>>1)+1;
     }
     else {
       //l = ceil(l / 2);
       l=(l>>1);
     }
     //ADRS.setTreeHeight(ADRS.getTreeHeight() + 1);
     height++;
     setTreeHeight(addr, height);
   }
   //return pk[0];
   memcpy(leaf, wots_pk, n);
}

/**
 * Computes the leaf at a given address. First generates the WOTS key pair, then computes leaf using l_tree. As this happens position independent, we only require that addr encodes the right ltree-address.
 */
static void gen_leaf_wots(unsigned char *leaf, const unsigned char *sk_seed, const xmssfast_params *params, const unsigned char *pub_seed, uint32_t ltree_addr[8], uint32_t ots_addr[8])
{
  unsigned char seed[params->n];
  unsigned char pk[params->wots_par.keysize];

  get_seed(seed, sk_seed, params->n, ots_addr);
  wots_pkgen(pk, seed, &(params->wots_par), pub_seed, ots_addr);

  l_tree(leaf, pk, params, pub_seed, ltree_addr);
}

static int treehash_minheight_on_stack(bds_state* state, const xmssfast_params *params, const treehash_inst *treehash) {
  unsigned int r = params->h, i;
  for (i = 0; i < treehash->stackusage; i++) {
    if (state->stacklevels[state->stackoffset - i - 1] < r) {
      r = state->stacklevels[state->stackoffset - i - 1];
    }
  }
  return r;
}

/**
 * Merkle's TreeHash algorithm. The address only needs to initialize the first 78 bits of addr. Everything else will be set by treehash.
 * Currently only used for key generation.
 *
 */
static void treehash_setup(unsigned char *node, int height, int index, bds_state *state, const unsigned char *sk_seed, const xmssfast_params *params, const unsigned char *pub_seed, const uint32_t addr[8])
{
  unsigned int idx = index;
  unsigned int n = params->n;
  unsigned int h = params->h;
  unsigned int k = params->k;
  // use three different addresses because at this point we use all three formats in parallel
  uint32_t ots_addr[8];
  uint32_t ltree_addr[8];
  uint32_t  node_addr[8];
  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);

  uint32_t lastnode, i;
  unsigned char stack[(height+1)*n];
  unsigned int stacklevels[height+1];
  unsigned int stackoffset=0;
  unsigned int nodeh;

  lastnode = idx+(1<<height);

  for (i = 0; i < h-k; i++) {
    state->treehash[i].h = i;
    state->treehash[i].completed = 1;
    state->treehash[i].stackusage = 0;
  }

  i = 0;
  for (; idx < lastnode; idx++) {
    setLtreeADRS(ltree_addr, idx);
    setOTSADRS(ots_addr, idx);
    gen_leaf_wots(stack+stackoffset*n, sk_seed, params, pub_seed, ltree_addr, ots_addr);
    stacklevels[stackoffset] = 0;
    stackoffset++;
    if (h - k > 0 && i == 3) {
      memcpy(state->treehash[0].node, stack+stackoffset*n, n);
    }
    while (stackoffset>1 && stacklevels[stackoffset-1] == stacklevels[stackoffset-2])
    {
      nodeh = stacklevels[stackoffset-1];
      if (i >> nodeh == 1) {
        memcpy(state->auth + nodeh*n, stack+(stackoffset-1)*n, n);
      }
      else {
        if (nodeh < h - k && i >> nodeh == 3) {
          memcpy(state->treehash[nodeh].node, stack+(stackoffset-1)*n, n);
        }
        else if (nodeh >= h - k) {
          memcpy(state->retain + ((1 << (h - 1 - nodeh)) + nodeh - h + (((i >> nodeh) - 3) >> 1)) * n, stack+(stackoffset-1)*n, n);
        }
      }
      setTreeHeight(node_addr, stacklevels[stackoffset-1]);
      setTreeIndex(node_addr, (idx >> (stacklevels[stackoffset-1]+1)));
      hash_h(stack+(stackoffset-2)*n, stack+(stackoffset-2)*n, pub_seed,
          node_addr, n);
      stacklevels[stackoffset-2]++;
      stackoffset--;
    }
    i++;
  }

  for (i = 0; i < n; i++)
    node[i] = stack[i];
}

static void treehash_update(treehash_inst *treehash, bds_state *state, const unsigned char *sk_seed, const xmssfast_params *params, const unsigned char *pub_seed, const uint32_t addr[8]) {
  int n = params->n;

  uint32_t ots_addr[8];
  uint32_t ltree_addr[8];
  uint32_t  node_addr[8];
  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);

  setLtreeADRS(ltree_addr, treehash->next_idx);
  setOTSADRS(ots_addr, treehash->next_idx);

  unsigned char nodebuffer[2 * n];
  unsigned int nodeheight = 0;
  gen_leaf_wots(nodebuffer, sk_seed, params, pub_seed, ltree_addr, ots_addr);
  while (treehash->stackusage > 0 && state->stacklevels[state->stackoffset-1] == nodeheight) {
    memcpy(nodebuffer + n, nodebuffer, n);
    memcpy(nodebuffer, state->stack + (state->stackoffset-1)*n, n);
    setTreeHeight(node_addr, nodeheight);
    setTreeIndex(node_addr, (treehash->next_idx >> (nodeheight+1)));
    hash_h(nodebuffer, nodebuffer, pub_seed, node_addr, n);
    nodeheight++;
    treehash->stackusage--;
    state->stackoffset--;
  }
  if (nodeheight == treehash->h) { // this also implies stackusage == 0
    memcpy(treehash->node, nodebuffer, n);
    treehash->completed = 1;
  }
  else {
    memcpy(state->stack + state->stackoffset*n, nodebuffer, n);
    treehash->stackusage++;
    state->stacklevels[state->stackoffset] = nodeheight;
    state->stackoffset++;
    treehash->next_idx++;
  }
}

/**
 * Computes a root node given a leaf and an authapth
 */
static void validate_authpath(unsigned char *root, const unsigned char *leaf, unsigned long leafidx, const unsigned char *authpath, const xmssfast_params *params, const unsigned char *pub_seed, uint32_t addr[8])
{
  unsigned int n = params->n;

  uint32_t i, j;
  unsigned char buffer[2*n];

  // If leafidx is odd (last bit = 1), current path element is a right child and authpath has to go to the left.
  // Otherwise, it is the other way around
  if (leafidx & 1) {
    for (j = 0; j < n; j++)
      buffer[n+j] = leaf[j];
    for (j = 0; j < n; j++)
      buffer[j] = authpath[j];
  }
  else {
    for (j = 0; j < n; j++)
      buffer[j] = leaf[j];
    for (j = 0; j < n; j++)
      buffer[n+j] = authpath[j];
  }
  authpath += n;

  for (i=0; i < params->h-1; i++) {
    setTreeHeight(addr, i);
    leafidx >>= 1;
    setTreeIndex(addr, leafidx);
    if (leafidx&1) {
      hash_h(buffer+n, buffer, pub_seed, addr, n);
      for (j = 0; j < n; j++)
        buffer[j] = authpath[j];
    }
    else {
      hash_h(buffer, buffer, pub_seed, addr, n);
      for (j = 0; j < n; j++)
        buffer[j+n] = authpath[j];
    }
    authpath += n;
  }
  setTreeHeight(addr, (params->h-1));
  leafidx >>= 1;
  setTreeIndex(addr, leafidx);
  hash_h(root, buffer, pub_seed, addr, n);
}

/**
 * Performs one treehash update on the instance that needs it the most.
 * Returns 1 if such an instance was not found
 **/
static char bds_treehash_update(bds_state *state, unsigned int updates, const unsigned char *sk_seed, const xmssfast_params *params, unsigned char *pub_seed, const uint32_t addr[8]) {
  uint32_t i, j;
  unsigned int level, l_min, low;
  unsigned int h = params->h;
  unsigned int k = params->k;
  unsigned int used = 0;

  for (j = 0; j < updates; j++) {
    l_min = h;
    level = h - k;
    for (i = 0; i < h - k; i++) {
      if (state->treehash[i].completed) {
        low = h;
      }
      else if (state->treehash[i].stackusage == 0) {
        low = i;
      }
      else {
        low = treehash_minheight_on_stack(state, params, &(state->treehash[i]));
      }
      if (low < l_min) {
        level = i;
        l_min = low;
      }
    }
    if (level == h - k) {
      break;
    }
    treehash_update(&(state->treehash[level]), state, sk_seed, params, pub_seed, addr);
    used++;
  }
  return updates - used;
}

/**
 * Updates the state (typically NEXT_i) by adding a leaf and updating the stack
 * Returns 1 if all leaf nodes have already been processed
 **/
static char bds_state_update(bds_state *state, const unsigned char *sk_seed, const xmssfast_params *params, unsigned char *pub_seed, const uint32_t addr[8]) {
  uint32_t ltree_addr[8];
  uint32_t node_addr[8];
  uint32_t ots_addr[8];

  int n = params->n;
  int h = params->h;
  int k = params->k;

  int nodeh;
  int idx = state->next_leaf;
  if (idx == 1 << h) {
    return 1;
  }

  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);
  
  setOTSADRS(ots_addr, idx);
  setLtreeADRS(ltree_addr, idx);

  gen_leaf_wots(state->stack+state->stackoffset*n, sk_seed, params, pub_seed, ltree_addr, ots_addr);

  state->stacklevels[state->stackoffset] = 0;
  state->stackoffset++;
  if (h - k > 0 && idx == 3) {
    memcpy(state->treehash[0].node, state->stack+state->stackoffset*n, n);
  }
  while (state->stackoffset>1 && state->stacklevels[state->stackoffset-1] == state->stacklevels[state->stackoffset-2]) {
    nodeh = state->stacklevels[state->stackoffset-1];
    if (idx >> nodeh == 1) {
      memcpy(state->auth + nodeh*n, state->stack+(state->stackoffset-1)*n, n);
    }
    else {
      if (nodeh < h - k && idx >> nodeh == 3) {
        memcpy(state->treehash[nodeh].node, state->stack+(state->stackoffset-1)*n, n);
      }
      else if (nodeh >= h - k) {
        memcpy(state->retain + ((1 << (h - 1 - nodeh)) + nodeh - h + (((idx >> nodeh) - 3) >> 1)) * n, state->stack+(state->stackoffset-1)*n, n);
      }
    }
    setTreeHeight(node_addr, state->stacklevels[state->stackoffset-1]);
    setTreeIndex(node_addr, (idx >> (state->stacklevels[state->stackoffset-1]+1)));
    hash_h(state->stack+(state->stackoffset-2)*n, state->stack+(state->stackoffset-2)*n, pub_seed, node_addr, n);

    state->stacklevels[state->stackoffset-2]++;
    state->stackoffset--;
  }
  state->next_leaf++;
  return 0;
}

/**
 * Returns the auth path for node leaf_idx and computes the auth path for the
 * next leaf node, using the algorithm described by Buchmann, Dahmen and Szydlo
 * in "Post Quantum Cryptography", Springer 2009.
 */
static void bds_round(bds_state *state, const unsigned long leaf_idx, const unsigned char *sk_seed, const xmssfast_params *params, unsigned char *pub_seed, uint32_t addr[8])
{
  unsigned int i;
  unsigned int n = params->n;
  unsigned int h = params->h;
  unsigned int k = params->k;

  unsigned int tau = h;
  unsigned int startidx;
  unsigned int offset, rowidx;
  unsigned char buf[2 * n];

  uint32_t ots_addr[8];
  uint32_t ltree_addr[8];
  uint32_t  node_addr[8];
  // only copy layer and tree address parts
  memcpy(ots_addr, addr, 12);
  // type = ots
  setType(ots_addr, 0);
  memcpy(ltree_addr, addr, 12);
  setType(ltree_addr, 1);
  memcpy(node_addr, addr, 12);
  setType(node_addr, 2);

  for (i = 0; i < h; i++) {
    if (! ((leaf_idx >> i) & 1)) {
      tau = i;
      break;
    }
  }

  if (tau > 0) {
    memcpy(buf,     state->auth + (tau-1) * n, n);
    // we need to do this before refreshing state->keep to prevent overwriting
    memcpy(buf + n, state->keep + ((tau-1) >> 1) * n, n);
  }
  if (!((leaf_idx >> (tau + 1)) & 1) && (tau < h - 1)) {
    memcpy(state->keep + (tau >> 1)*n, state->auth + tau*n, n);
  }
  if (tau == 0) {
    setLtreeADRS(ltree_addr, leaf_idx);
    setOTSADRS(ots_addr, leaf_idx);
    gen_leaf_wots(state->auth, sk_seed, params, pub_seed, ltree_addr, ots_addr);
  }
  else {
    setTreeHeight(node_addr, (tau-1));
    setTreeIndex(node_addr, leaf_idx >> tau);
    hash_h(state->auth + tau * n, buf, pub_seed, node_addr, n);
    for (i = 0; i < tau; i++) {
      if (i < h - k) {
        memcpy(state->auth + i * n, state->treehash[i].node, n);
      }
      else {
        offset = (1 << (h - 1 - i)) + i - h;
        rowidx = ((leaf_idx >> i) - 1) >> 1;
        memcpy(state->auth + i * n, state->retain + (offset + rowidx) * n, n);
      }
    }

    for (i = 0; i < ((tau < h - k) ? tau : (h - k)); i++) {
      startidx = leaf_idx + 1 + 3 * (1 << i);
      if (startidx < 1U << h) {
        state->treehash[i].h = i;
        state->treehash[i].next_idx = startidx;
        state->treehash[i].completed = 0;
        state->treehash[i].stackusage = 0;
      }
    }
  }
}

/*
 * Generates a XMSS key pair for a given parameter set.
 * Format sk: [(32bit) idx || SK_SEED || SK_PRF || PUB_SEED || root]
 * Format pk: [root || PUB_SEED] omitting algo oid.
 */
int xmssfast_Genkeypair(unsigned char *pk, unsigned char *sk, bds_state *state, unsigned char *seed, unsigned char h)
{
  xmssfast_set_params(&paramsfast, 32, h, 16, 2);
  unsigned int k = paramsfast.k;
  unsigned int n = paramsfast.n;
  unsigned char stack[(h+1)*n];
  unsigned int stackoffset = 0;
  unsigned char stacklevels[h+1];
  unsigned char auth[(h)*n];
  unsigned char keep[(h >> 1)*n];
  treehash_inst treehash[h-k];
  unsigned char th_nodes[(h-k)*n];
  unsigned char retain[((1 << k) - k - 1)*n];
  for (int i = 0; i < h-k; i++)
    treehash[i].node = &th_nodes[n*i];
  xmss_set_bds_state(state, stack, stackoffset, stacklevels, auth, keep, treehash, retain, 0);

  // Set idx = 0
  sk[0] = 0;
  sk[1] = 0;
  sk[2] = 0;
  sk[3] = 0;
  // Init SK_SEED (n byte), SK_PRF (n byte), and PUB_SEED (n byte)
  //randombytes(sk+4, 3*n);
  // Copy PUB_SEED to public key
  unsigned char randombits[3 * n];
  shake256(randombits, 3 * n, seed, n);
  size_t rnd = 96;
  size_t pks = 32;
  memcpy(sk + 4, randombits, rnd);
  memcpy(pk + n, sk + 4 + 2 * n, pks);

  uint32_t addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  // Compute root
  treehash_setup(pk, paramsfast.h, 0, state, sk+4, &paramsfast, sk+4+2*n, addr);
  // copy root to sk
  memcpy(sk+4+3*n, pk, pks);
  return 0;
}