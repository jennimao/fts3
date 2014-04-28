# Copyright notice:
# Copyright (C) Members of the EMI Collaboration, 2010.
#
# See www.eu-emi.eu for details on the copyright holders
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
from django.db.models import Q
from ftsweb.models import ConfigAudit
from ftsweb.models import ProfilingSnapshot, ServerConfig
from ftsweb.models import LinkConfig, ShareConfig
from ftsweb.models import DebugConfig, Optimize
from jsonify import jsonify, jsonify_paged
from util import getOrderBy, orderedField

@jsonify_paged
def audit(httpRequest):
    ca = ConfigAudit.objects
    
    if httpRequest.GET.get('action', None):
        ca = ca.filter(action = httpRequest.GET['action'])
    if httpRequest.GET.get('user', None):
        ca = ca.filter(dn = httpRequest.GET['user'])
    if httpRequest.GET.get('contains', None):
        ca = ca.filter(config__icontains = httpRequest.GET['contains'])
    
    return ca.order_by('-datetime')

@jsonify
def server(httpRequest):
    server_config = ServerConfig.objects.all()
    if len(server_config) > 0:
        return server_config[0]
    else:
        return {}


# Wrap a list of link config, and push the
# vo shares on demand (lazy!)
class AppendShares:
    
    def __init__(self, resultSet):
        self.rs = resultSet
        
    def __len__(self):
        return len(self.rs)
    
    def __getitem__(self, i):
        for link in self.rs[i]:
            shares = ShareConfig.objects.filter(source = link.source, destination = link.destination).all()
            link.shares = {}
            for share in shares:
                link.shares[share.vo] = share.active
            yield link

@jsonify_paged
def links(httpRequest):
    links = LinkConfig.objects
    
    if httpRequest.GET.get('source_se'):
        links = links.filter(source = httpRequest.GET['source_se'])
    if httpRequest.GET.get('dest_se'):
        links = links.filter(destination = httpRequest.GET['dest_se'])
    
    return AppendShares(links.all())

@jsonify
def debug(httpRequest):
    return DebugConfig.objects.all()

@jsonify_paged
def limits(httpRequest):
    max_cfg = Optimize.objects.filter(Q(active__isnull = False) | Q(bandwidth__isnull = False))
    
    (orderBy, orderDesc) = getOrderBy(httpRequest)
    if orderBy == 'bandwidth':
        max_cfg = max_cfg.order_by(orderedField('bandwidth', orderDesc))
    elif orderBy == 'active':
        max_cfg = max_cfg.order_by(orderedField('active', orderDesc))
    elif orderBy == 'source_se':
        max_cfg = max_cfg.order_by(orderedField('source_se', orderDesc))
    elif orderBy == 'dest_se':
        max_cfg = max_cfg.order_by(orderedField('dest_se', orderDesc))
    else:
        max_cfg = max_cfg.order_by('-active')
    
    return max_cfg

