import puppeteer, { JSHandle } from 'puppeteer';
import {readFileSync, rmdir,createWriteStream, mkdir ,readdir ,stat}  from 'fs';
import {promisify} from 'util';
import {join, resolve} from 'path';
import uuidv1 from 'uuid/v1';
import https from 'https';
import http from 'http';
var esprima = require('esprima');

const rmdirAsync = promisify(rmdir);
const mkdirAsync = promisify(mkdir)
const readdirAsync = promisify(readdir);
const statAsync = promisify(stat);

declare global {
    interface Window { WebAssemblyCallLocations: any; }
}

interface WebAssemblyInstrumentation {
    instantiate: any[],
    instantiateStreaming: any[],
    exportCalls: any,
    importCalls: any,
    altered: boolean,
    addExport: Function,
    addImport: Function,
    addInstantiate: Function,
    addInstantiateStreaming: Function
}
const preloadFile = readFileSync(join(__dirname, './instrumentationCode.js'), 'utf8');

async function getFiles(dir: string): Promise<string[]> {
    const subdirs = await readdirAsync(dir);
    const files = await Promise.all(
        subdirs.map(async (subdir) => {
        const res = resolve(dir, subdir);
        return (await statAsync(res)).isDirectory() ? getFiles(res) : res;
        })
    );
    // @ts-ignore
    return files.reduce((a, f) => a.concat(f), []);
}

class Crawler{
    browser?: puppeteer.Browser = undefined;
    webAssemblyWorkers: JSHandle[] = []; //Holds the JSHandles of the instrumentation objects used to store tarces in WebWorkers
    allJSONOfRecordedWorkers: WebAssemblyInstrumentation[] = []; //Holds the JSONed versions of the instrumentation objects
    capturedRequests: any = {};
    currentURL: string = '';
    userDataDirPath:string  = ''
    

    async downloadFile(url: string, dest: string): Promise<String> {
        //Fetched from https://stackoverflow.com/questions/11944932/how-to-download-a-file-with-node-js-without-using-third-party-libraries
        const fetchingLibrary = url.includes('https:') ? https : http; 
        return new Promise((resolve, reject) => {
            var file = createWriteStream(dest);
            var request = fetchingLibrary.get(url, function (response) {
                    response.pipe(file);
                    file.on('finish', function () {
                        // @ts-ignore
                        file.close(() => {
                            resolve(dest);
                        });
                    });
    
                    file.on('error', function (err) {
                        reject(err);
                    });
                })
                .on('error', function (err) {
                    reject(err);
                });
        })
    
    }
    
    getFileInURL(url: string) {
        const lastSlashIndex = url.lastIndexOf('/') + 1;
        const queryStringIndex = url.lastIndexOf('?');
        if (queryStringIndex == -1) {
            return url.substring(lastSlashIndex);
        } else {
            return url.substring(lastSlashIndex, queryStringIndex);
        }
    
    }

    async getBrowser(): Promise<puppeteer.Browser>{
        if(this.browser != null){
            return this.browser;
        }

        const userDataDir = uuidv1().split('-')[0];
        this.userDataDirPath = userDataDir;
        const chromeArgs = [
            '--disable-background-timer-throttling',
            '--disable-backgrounding-occluded-windows',
            '--disable-renderer-backgrounding',
            '--disable-gpu',
            '--no-sandox'
          ];
        const browser = await puppeteer.launch({
            userDataDir,
            // args: chromeArgs,
            dumpio: true,
            // headless: false,
            devtools: true,

        });
        this.browser = browser;
        return this.browser;
    }

    async getPage(): Promise<puppeteer.Page>{
        const browser: puppeteer.Browser = await this.getBrowser();

        const page: puppeteer.Page = await browser.newPage();
        await page.setRequestInterception(true);
        page.on('request', (interceptedRequest) => {
            let requestURL = interceptedRequest.url()
            const resourceType = interceptedRequest.resourceType();
            if (
                resourceType == 'script' ||
                resourceType == 'document' /* ||
                requestURL.includes('.js') ||
                requestURL.includes('.html') */
            ) {
                this.capturedRequests[this.currentURL].push(requestURL);
            }
            interceptedRequest.continue();
        });
        page.evaluateOnNewDocument(preloadFile)
        page.on('workercreated', async worker => {
            // console.log('Worker created: ' + worker.url())
            await worker.evaluate(preloadFile)
            try{
                await worker.evaluate(() => {
                    setTimeout(()=>{
                        console.log(self);
                    },1000)
                })
                var currentWorkerWebAssembly = await worker.evaluateHandle(() => {
                    
                    return self.WebAssemblyCallLocations;
                })

                this.webAssemblyWorkers.push(currentWorkerWebAssembly);
            } catch(err){
                console.error('Worker Eval', err)
            }

            setTimeout(async () => {
                try{
                    const workerWebAssemblyJson: WebAssemblyInstrumentation[] = [];
                    for(const x of this.webAssemblyWorkers){
                        let workerObject = await x.jsonValue();
                        this.formatInstrumentObject(workerObject);
                        workerWebAssemblyJson.push(workerObject);
                    }
                    this.allJSONOfRecordedWorkers.push(...workerWebAssemblyJson)
                } catch(error){
                    console.log(error);
                }
            }, 100)

        });


        return page;
    }

    async closeBrowser() : Promise<void>{
        if(this.browser != null ){
            await this.browser.close();
        }

        try{
            // @ts-ignore
            await rmdirAsync(this.userDataDirPath, {
                recursive : true
            })
        } catch(e){
            console.error(e);
        }
    }

    formatStackTrace(stackTrace: string) {
        let stackTraceFrames = stackTrace.replace('Error\n ', '')
                                .replace(/Object\./g, '')
                                .split(/at(.*)(\(.*\))?/g)
                                .filter(str => {
                                    return str !== undefined && str.match(/\S/g) != null
                                });
        const fromattedstackTraceFrames = stackTraceFrames.map((frame, index) => {
                // frame = frame.replace(/Object\.newInstance\.exports\.<computed> \[as (.*)\]/g, "$1")
                if(frame.includes('__puppeteer_evaluation_script__')){
                    return null;
                }

                if(frame.match(/<anonymous>:.*/)){
                    return null;
                }

                if(frame.includes('closureReturn')){
                    return null;
                }
                
                frame = frame.replace(/(\(.*\))/g, "");
                if(index === 0){
                    frame = frame.trim();
                    frame = frame.replace(/^Object\./, '');

                }
                frame = frame.trim();

                return frame;
            })
            .filter(str => str != null);

        return fromattedstackTraceFrames;
    }

    formatInstrumentObject(webassemblyObject: any){
        if (webassemblyObject.instantiate != null) {
            webassemblyObject.instantiate = webassemblyObject.instantiate.map(this.formatStackTrace);
        }

        if (webassemblyObject.instantiateStreaming != null) {
            webassemblyObject.instantiateStreaming = webassemblyObject.instantiateStreaming.map(this.formatStackTrace);
        }

        if (webassemblyObject.exportCalls != null) {
            let newObj: any = {};
            for (let funcName in webassemblyObject.exportCalls) {
                let stacks = webassemblyObject.exportCalls[funcName];

                newObj[funcName] = stacks.map((stack:string) => {
                    const formattedTraces = this.formatStackTrace(stack);
                    formattedTraces.unshift(funcName);
                    return formattedTraces;
                });
            }

            webassemblyObject.exportCalls = newObj;
        }

        if (webassemblyObject.importCalls != null) {
            let newObj:any = {};
            for (let funcName in webassemblyObject.importCalls) {
                let stacks = webassemblyObject.importCalls[funcName];

                newObj[funcName] = stacks.map((stack: string) => {
                    const formattedTraces = this.formatStackTrace(stack);
                    formattedTraces.unshift(funcName);
                    return formattedTraces;
                });
            }

            webassemblyObject.importCalls = newObj;
        }
    }

    cleanURL(url: string){
        url = url.replace('://', '____').replace(/\//g, '__').replace(/\./g, '_____');
        return url;
    }

    async main(){

        const page = await this.getPage();
        this.currentURL = 'http://webdollar.io';
        this.capturedRequests[this.currentURL] = [];
        let windowWebAssemblyHandle: WebAssemblyInstrumentation | null  = null;
    
        const finish = async () => {
            try{
                windowWebAssemblyHandle = await (await page.evaluateHandle(() => window.WebAssemblyCallLocations)).jsonValue();
                this.formatInstrumentObject(windowWebAssemblyHandle);
            } catch(e){
                console.error(e)
            }
    
            if (this.webAssemblyWorkers.length > 0) {
                try{
                    const workerWebAssemblyJson: WebAssemblyInstrumentation[] = [];
                    for(const x of this.webAssemblyWorkers){
                        let workerObject = await x.jsonValue();
                        this.formatInstrumentObject(workerObject);
                        workerWebAssemblyJson.push(workerObject);
                    }
                    this.allJSONOfRecordedWorkers.push(...workerWebAssemblyJson);
                } catch(error){
                    console.error(error);
                }
                
            }
        }

        // const pageTimer = setTimeout(async ()=> {
        //     await finish();
        //     await page.close();
        //     await this.closeBrowser();

        // }, 30*1000 );

        await page.goto(this.currentURL,{
            waitUntil: 'load'
        });
        await page.waitFor(12 * 1000);

        await finish();

        // clearTimeout(pageTimer);

        console.log(windowWebAssemblyHandle);
        console.log(this.allJSONOfRecordedWorkers);

        const cleanedURL = this.cleanURL(this.currentURL);

        try{
            await mkdirAsync(join('./JSDownloads', cleanedURL));
        } catch(mkdirError){
            if(mkdirError.code !== 'EEXIST'){
                console.error(mkdirError);
            }
        }
        for(const request of this.capturedRequests[this.currentURL]){
            try{
                const destination = join('./JSDownloads', cleanedURL, this.cleanURL(request));
                await this.downloadFile(request, destination)
            } catch(downloadError){
                console.error(downloadError);
            }
            
        }
        await page.close();
        await this.closeBrowser();

        
    }
}


const crawler = new Crawler();
crawler.main();