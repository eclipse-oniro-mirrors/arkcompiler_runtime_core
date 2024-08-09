/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

export const ts_string = 'string';
export const ts_number = 1;
type UnionType = string | number;

type LiteralType = 1 | 'string';

export class LiteralClass<T extends LiteralType> {
	private _value: T;

	constructor(value: T) {
		this._value = value;
	}

	set(arg: T): void {
		this._value = arg;
	}

	get(): T {
		return this._value;
	}
}

export class UnionClass<T extends UnionType> {
	private _value: T;

	constructor(value: T) {
		this._value = value;
	}

	set(arg: T): void {
		this._value = arg;
	}

	get(): T {
		return this._value;
	}
}

interface GInterface<T> {
	value: T;
}

export class InterfaceClass<T> implements GInterface<T> {
	public value: T;

	constructor(value: T) {
		this.value = value;
	}

	set(arg: T): void {
		this.value = arg;
	}

	get(): T {
		return this.value;
	}
}

export abstract class GAbstract<T> {
	private _value: T;

	constructor(value: T) {
		this._value = value;
	}

	set(arg: T): void {
		this._value = arg;
	}

	get(): T {
		return this._value;
	}
}

export class AbstractClass<T> extends GAbstract<T> {
	constructor(value: T) {
		super(value);
	}
}

export class GClass<T> {
	public content: T;

	constructor(content: T) {
		this.content = content;
	}

	public get(): T {
		return this.content;
	}
}

export function generic_function<T>(arg: T): T {
	return arg;
}

export function tuple_declared_type<T, U>(items: [T, U]): [T, U] {
	return items;
}

interface Data {
	data: string;
}

export function generic_subset_ref<T extends Data>(items: T): T {
	return items;
}

type TGenericFn<T> = () => T;

export const explicitly_declared_type: TGenericFn<string> = () => {
	return ts_string;
};

export const literalClass = new LiteralClass(ts_string);
export const unionClass = new UnionClass<string>(ts_string);
export const interfaceClass = new InterfaceClass<string>(ts_string);
export const abstractClass = new AbstractClass<string>(ts_string);
