export enum Owner {client, server, both};
export enum Mode {workspace, file};

export interface Config {
  path?:string,
  runtimeOwner?: Owner,
  controlOwner?: Owner,
  port?:number,
  editor?: boolean,
  root?: string,
  eveRoot?: string,
  internal?: boolean,
  mode?: Mode
  initDB?:any
}

export var config:Config = {};

export function init(opts:Config) {
  for(let key in opts) {
    config[key] = opts[key];
  }
}
