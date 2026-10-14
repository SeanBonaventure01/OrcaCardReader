import { render } from 'preact';

import { App } from './App';
import { connectLive } from './store';
import './styles.css';

connectLive();
render(<App />, document.getElementById('app')!);
