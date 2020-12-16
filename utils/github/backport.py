# -*- coding: utf-8 -*-

from clickhouse.utils.github.cherrypick import CherryPick
from clickhouse.utils.github.query import Query as RemoteRepo
from clickhouse.utils.github.local import Repository as LocalRepo

import argparse
import logging
import re
import sys


class Backport:
    def __init__(self, token, owner, name, team):
        self._gh = RemoteRepo(token, owner=owner, name=name, team=team, max_page_size=30, min_page_size=7)
        self._token = token
        self.default_branch_name = self._gh.default_branch
        self.ssh_url = self._gh.ssh_url

    def getPullRequests(self, from_commit):
        return self._gh.get_pull_requests(from_commit)

    def getBranchesWithLTS(self, release_branches):
        # it's convenient to return branches keeping their precedence order
        branches = []  # [(branch_name, base_commit)]
        for pull_request in self._gh.find_pull_requests("release-lts"):
            if not pull_request['merged'] and not pull_request['closed']:
                for branch in release_branches:
                    if branch[0] == pull_request['headRefName']:
                        branches.append(branch)
                        break
        return branches

    def execute(self, repo, until_commit, number, find_lts, run_cherrypick):
        repo = LocalRepo(repo, 'origin', self.default_branch_name)
        all_branches = repo.get_release_branches()

        # assume that LTS branches are always older than recent ones
        branches = repo.get_release_branches()[-number:]  # [(branch_name, base_commit)]
        branches_lts = self.getBranchesWithLTS(all_branches) if find_lts else []
        branches_lts.extend(branch for branch in branches if branch not in branches_lts)
        branches = branches_lts

        if not branches:
            logging.info('No release branches found!')
            return

        for branch in branches:
            logging.info('Found release branch: %s', branch[0])

        if not until_commit:
            until_commit = branches[0][1]
        pull_requests = self.getPullRequests(until_commit)

        backport_map = {}

        RE_MUST_BACKPORT = re.compile(r'^v(\d+\.\d+)-must-backport$')
        RE_NO_BACKPORT = re.compile(r'^v(\d+\.\d+)-no-backport$')
        RE_BACKPORTED = re.compile(r'^v(\d+\.\d+)-backported$')

        # pull-requests are sorted by ancestry from the least recent.
        for pr in pull_requests:
            while repo.comparator(branches[-1][1]) >= repo.comparator(pr['mergeCommit']['oid']):
                logging.info("PR #{} is already inside {}. Dropping this branch for further PRs".format(pr['number'], branches[-1][0]))
                branches.pop()

            logging.info("Processing PR #{}".format(pr['number']))

            assert len(branches)

            branch_set = set([branch[0] for branch in branches])

            # First pass. Find all must-backports
            for label in pr['labels']['nodes']:
                if label['name'] == 'pr-bugfix':
                    backport_map[pr['number']] = branch_set.copy()
                    continue
                matched = RE_MUST_BACKPORT.match(label['name'])
                if matched:
                    if pr['number'] not in backport_map:
                        backport_map[pr['number']] = set()
                    backport_map[pr['number']].add(matched.group(1))

            # Second pass. Find all no-backports
            for label in pr['labels']['nodes']:
                if label['name'] == 'pr-no-backport' and pr['number'] in backport_map:
                    del backport_map[pr['number']]
                    break
                matched_no_backport = RE_NO_BACKPORT.match(label['name'])
                matched_backported = RE_BACKPORTED.match(label['name'])
                if matched_no_backport and pr['number'] in backport_map and matched_no_backport.group(1) in backport_map[pr['number']]:
                    backport_map[pr['number']].remove(matched_no_backport.group(1))
                    logging.info('\tskipping %s because of forced no-backport', matched_no_backport.group(1))
                elif matched_backported and pr['number'] in backport_map and matched_backported.group(1) in backport_map[pr['number']]:
                    backport_map[pr['number']].remove(matched_backported.group(1))
                    logging.info('\tskipping %s because it\'s already backported manually', matched_backported.group(1))

        for pr, branches in list(backport_map.items()):
            logging.info('PR #%s needs to be backported to:', pr)
            for branch in branches:
                logging.info('\t%s, and the status is: %s', branch, run_cherrypick(self._token, pr, branch))

        # print API costs
        logging.info('\nGitHub API total costs per query:')
        for name, value in list(self._gh.api_costs.items()):
            logging.info('%s : %s', name, value)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--token',     type=str, required=True, help='token for Github access')
    parser.add_argument('--repo',      type=str, required=True, help='path to full repository', metavar='PATH')
    parser.add_argument('--til',       type=str,                help='check PRs from HEAD til this commit', metavar='COMMIT')
    parser.add_argument('-n',          type=int, dest='number', help='number of last release branches to consider')
    parser.add_argument('--dry-run',   action='store_true',     help='do not create or merge any PRs', default=False)
    parser.add_argument('--verbose', '-v', action='store_true', help='more verbose output', default=False)
    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(format='%(message)s', stream=sys.stdout, level=logging.DEBUG)
    else:
        logging.basicConfig(format='%(message)s', stream=sys.stdout, level=logging.INFO)

    cherrypick_run = lambda token, pr, branch: CherryPick(token, 'ClickHouse', 'ClickHouse', 'core', pr, branch).execute(args.repo, args.dry_run)
    bp = Backport(args.token, 'ClickHouse', 'ClickHouse', 'core')
    bp.execute(args.repo, args.til, args.number, cherrypick_run)
