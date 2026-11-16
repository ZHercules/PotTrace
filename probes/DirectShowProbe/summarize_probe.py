"""Summarize observed boundaries; deliberately does not certify load timestamps."""
import argparse,json
from pathlib import Path

def summarize(folder):
    folder=Path(folder).resolve()
    report=json.loads((folder/'result.json').read_text(encoding='utf-8'))
    events=[json.loads(line) for line in (folder/'graph.jsonl').read_text(encoding='utf-8').splitlines()] if (folder/'graph.jsonl').exists() else []
    callbacks=[line.split('\t') for line in (folder/'script.tsv').read_text(encoding='utf-8').splitlines()] if (folder/'script.tsv').exists() else []
    result={k:report.get(k) for k in ['baseline','preload','held_preload','format','error_reopen','actions_completed','basic_probe_passed','deployment_eligible','failure','exit_code','forced_termination','graph_event_count','log_lost','missing_expected_sources','missing_destroy_records','graph_enumeration_uncertain','unexpected_next_while_held','invalid_connected_runs','stop_boundary_found','reopen_identity_found','recovery_identity_found']}
    result['evidence']=folder.name;result['player_fixture']=Path(report['player']).parent.name
    result['startup_first_active_ms']=report['first_active']['at']-report['start'] if report.get('first_active') else None
    result['sources']=[]
    for path in report['sources']:
        connected=[e for e in events if e['inputConnected'] and e['outputConnected'] and e.get('upstreamSource')==path and e.get('upstreamResult',-1)>=0]
        row={'source':str(Path(path).relative_to(folder))}
        for kind in ['PauseAfter','RunAfter','StopAfter']:
            row[kind+'_ms']=next((e['at']-report['start'] for e in connected if e['event']==kind),None)
        for kind in ['Open','Start']:
            row['script_'+kind+'_ms']=next((int(e[0])-report['start'] for e in callbacks if len(e)>=5 and e[1]==kind and e[4]==path),None)
        row['script_start_minus_graph_run_ms']=row['script_Start_ms']-row['RunAfter_ms'] if row['script_Start_ms'] is not None and row['RunAfter_ms'] is not None else None
        row['renderer_roots']=sorted({w['root'] for e in connected for w in e['videoWindows']})
        row['renderer_matches_owned_window']=bool(row['renderer_roots']) and row['renderer_roots']==[report['main_hwnd']]
        result['sources'].append(row)
    inspected=[e['inspectionEndedMs']-e['inspectionStartedMs'] for e in events if e.get('identityInspected') and e.get('inspectionEndedMs') is not None]
    result['max_inspection_tick_ms']=max(inspected,default=None)
    result['enum_end_results']=sorted({e['enumEnd'] for e in events if e['event']=='RunAfter' and 'enumEnd' in e})
    result['renderer_capabilities']=list({json.dumps(c,sort_keys=True):c for e in events if e['event']=='RunAfter' for c in e.get('windowCapabilities',[]) if 'Renderer' in c['filter']}.values())
    result['query_timeouts']=sum(bool(row.get('query_timeout')) for key,rows in report.items() if key.endswith('_samples') for row in rows)
    if report.get('held_after'):
        result['held_observed_ms']=report['held_after']['state']['at']-report['held_before']['state']['at']
        result['held_colors']=[report['held_before']['color'],report['held_after']['color']]
        result['next_color']=report.get('next_display',{}).get('color')
        result['next_file_open_while_held']=all(report['sources'][3] in report[key].get('open_fixture_files',report[key].get('open_files',[])) for key in ['held_before','held_after'])
        pauses=[e for e in events if e['event']=='PauseAfter' and e.get('upstreamSource')==report['sources'][2] and report['held_pause_started']-100<=e['at']<=report['held_pause_started']]
        if pauses:
            start=pauses[-1]['at'];end=next((e['at'] for e in events if e['event']=='RunAfter' and e.get('upstreamSource')==report['sources'][2] and e['at']>start),None)
            result['graph_held_pause_ms']=None if end is None else end-start
        result['held_position_tail_range_ms']=[min(r['position'] for r in report['held_samples'][50:] if r['position'] is not None),max(r['position'] for r in report['held_samples'][50:] if r['position'] is not None)]
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folders',nargs='+');parser.add_argument('--output',type=Path);args=parser.parse_args()
    result=[summarize(folder) for folder in args.folders]
    text=json.dumps(result,indent=2,ensure_ascii=False)
    if args.output:args.output.write_text(text+'\n',encoding='utf-8')
    else:print(text)
