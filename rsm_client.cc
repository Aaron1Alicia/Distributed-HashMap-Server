#include "rsm_client.h"
#include <vector>
#include <arpa/inet.h>
#include <stdio.h>
#include <handle.h>
#include <unistd.h>
#include "lang/verify.h"


rsm_client::rsm_client(std::string dst)
{
	printf("create rsm_client\n");
	std::vector<std::string> mems;

	pthread_mutex_init(&rsm_client_mutex, NULL);
	sockaddr_in dstsock;
	make_sockaddr(dst.c_str(), &dstsock);
	primary = dst;
	{
		ScopedLock ml(&rsm_client_mutex);
		VERIFY (init_members());
	}
	printf("rsm_client: done\n");
}

// Assumes caller holds rsm_client_mutex 
void
rsm_client::primary_failure()
{
	// You fill this in for Lab 3
//	std::string new_primary;
//	assert(known_mems.back()!=primary);
//	primary = known_mems.back();
	for (auto iter = known_mems.begin();iter!=known_mems.end();iter++) {
			if(*iter==primary) {
				known_mems.erase(iter);
				break;
			}
		}

	for (auto iter = known_mems.begin();iter!=known_mems.end();iter++) {
		if(*iter!=primary) {
			primary = *iter;
			break;
		}
	}
	//known_mems.pop_back();

}

rsm_protocol::status
rsm_client::invoke(int proc, std::string req, std::string &rep)
{
	int ret;
	ScopedLock ml(&rsm_client_mutex);

	while (1) {
		handle h(primary);

		VERIFY(pthread_mutex_unlock(&rsm_client_mutex)==0);
		rpcc *cl = h.safebind();
		if (cl) {
			ret = cl->call(rsm_client_protocol::invoke, proc, req, 
					rep, rpcc::to(5000));
		}
		VERIFY(pthread_mutex_lock(&rsm_client_mutex)==0);

		printf("rsm_client::invoke proc %x primary %s ret %d\n", proc, 
				primary.c_str(), ret);

		if (!cl || ret == rpc_const::timeout_failure 
					|| ret == rpc_const::oldsrv_failure ) {
			std::string old_primary;
			primary_failure();
			printf ("rsm_client::invoke: ret=%d old primary %s failed, new one is %s, fail this ppc to client\n",
					ret, old_primary.c_str(), primary.c_str());
			break;
		}
	
		if (ret == rsm_client_protocol::OK) {
			break;
		} else if (ret == rsm_client_protocol::BUSY) {
			printf("rsm is busy %s\n", primary.c_str());
			sleep(3);
			continue;
		} else if (ret == rsm_client_protocol::NOTPRIMARY) {
			printf("primary %s isn't the primary--let's get a complete list of mems\n", 
					primary.c_str());
			if (init_members())
				continue;
		} else {
			printf("rsm_client::invoke, bounce failure %d to user\n", ret);
			break;
		}
	}
	printf("rsm_client::invoke finished proc %x primary %s ret %d\n", proc, primary.c_str(), ret);
	return ret;
}

bool
rsm_client::init_members()
{
	printf("rsm_client::init_members get members!\n");
	handle h(primary);
	std::vector<std::string> new_view;
	VERIFY(pthread_mutex_unlock(&rsm_client_mutex)==0);
	int ret;
	rpcc *cl = h.safebind();
	if (cl) {
		ret = cl->call(rsm_client_protocol::members, 0, new_view, 
				rpcc::to(1000)); 
	}
	VERIFY(pthread_mutex_lock(&rsm_client_mutex)==0);
	if (cl == 0 || ret != rsm_protocol::OK)
		return false;
	if (new_view.size() < 1) {
		printf("rsm_client::init_members do not know any members!\n");
		VERIFY(0);
	}
	known_mems = new_view;
	primary = known_mems.back();
	known_mems.pop_back();

	printf("rsm_client::init_members: primary %s\n", primary.c_str());

	return true;
}

