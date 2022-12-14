/*
   Unix SMB/CIFS implementation.
   async lookupuseraliases
   Copyright (C) Volker Lendecke 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "winbindd.h"
#include "librpc/gen_ndr/ndr_winbind_c.h"
#include "libcli/security/dom_sid.h"

struct wb_lookupuseraliases_state {
	struct tevent_context *ev;
	struct wbint_SidArray sids;
	struct wbint_RidArray rids;
};

static void wb_lookupuseraliases_done(struct tevent_req *subreq);

struct tevent_req *wb_lookupuseraliases_send(TALLOC_CTX *mem_ctx,
					     struct tevent_context *ev,
					     struct winbindd_domain *domain,
					     uint32_t num_sids,
					     const struct dom_sid *sids)
{
	struct tevent_req *req, *subreq;
	struct wb_lookupuseraliases_state *state;
	uint32_t i;

	req = tevent_req_create(mem_ctx, &state,
				struct wb_lookupuseraliases_state);
	if (req == NULL) {
		return NULL;
	}

	D_INFO("WB command lookupuseraliases start.\n"
	       "Query domain %s for max %"PRIu32" SID(s).\n",
	       domain->name, num_sids);

	for (i = 0; i < num_sids; i++) {
		struct dom_sid_buf buf;
		D_INFO("%"PRIu32": SID %s\n", i, dom_sid_str_buf(&sids[i], &buf));
	}
	state->sids.num_sids = num_sids;
	state->sids.sids = discard_const_p(struct dom_sid, sids);

	subreq = dcerpc_wbint_LookupUserAliases_send(
		state, ev, dom_child_handle(domain), &state->sids, &state->rids);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, wb_lookupuseraliases_done, req);
	return req;
}

static void wb_lookupuseraliases_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct wb_lookupuseraliases_state *state = tevent_req_data(
		req, struct wb_lookupuseraliases_state);
	NTSTATUS status, result;

	status = dcerpc_wbint_LookupUserAliases_recv(subreq, state, &result);
	TALLOC_FREE(subreq);
	if (any_nt_status_not_ok(status, result, &status)) {
		D_WARNING("LookupUserAliases failed with %s.\n",
			  nt_errstr(status));
		tevent_req_nterror(req, status);
		return;
	}
	tevent_req_done(req);
}

NTSTATUS wb_lookupuseraliases_recv(struct tevent_req *req, TALLOC_CTX *mem_ctx,
				   uint32_t *num_aliases, uint32_t **aliases)
{
	struct wb_lookupuseraliases_state *state = tevent_req_data(
		req, struct wb_lookupuseraliases_state);
	NTSTATUS status;
	uint32_t i;

	if (tevent_req_is_nterror(req, &status)) {
		return status;
	}
	*num_aliases = state->rids.num_rids;
	D_INFO("WB command lookupuseraliases end.\nGot %"PRIu32" alias(es):\n",
	       *num_aliases);
	for (i = 0; i < *num_aliases; i++) {
		D_INFO("%"PRIu32": RID %"PRIu32"\n", i, state->rids.rids[i]);
	}

	*aliases = talloc_move(mem_ctx, &state->rids.rids);
	return NT_STATUS_OK;
}
