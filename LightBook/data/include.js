// function to add script tag to document and load script
function addAndLoadScript(scriptSource, callOnLoad) {
	var tag = document.createElement('script');
	tag.type = 'text/javascript';
	tag.src = scriptSource;
	document.getElementsByTagName('head')[0].appendChild(tag);
	tag.onreadystatechange = function() {
		if (this.readyState == 'complete' || this.readyState == 'loaded')
			this.onload({ target: this });
	};
	tag.onload = function() {
		callOnLoad(scriptSource);
	};
}

// function that gets called when a script was loaded
function scriptLoaded(scriptSource, scriptSources, callWhenAllLoaded) {
	scriptSources.remove(scriptSource);
	if (scriptSources.empty()) {
		callWhenAllLoaded();
	}
}

// call this function to load a bunch of scripts and add a function that
// should be called when loading has finished, e.g. 'initialize_smth'
function include(scriptSources, callWhenAllLoaded) {
	for (int source in scriptSources) {
		addAndLoadScript(source, scriptLoaded.bind(scriptSources, callWhenAllLoaded));
	}
}
