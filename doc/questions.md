# Outstanding questions related to dynamic assignment of
  endpoints/completion queues to threads.

Can one completion queue belong to more than one poll set?
Probably not.  Documentation is silent on this point.

There is no way to fi_wait on a poll set?

Can I assign a new wait set to a completion queue?
