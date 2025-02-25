<?php
/**
 * This file is part of OpenMediaVault.
 *
 * @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    Volker Theile <volker.theile@openmediavault.org>
 * @copyright Copyright (c) 2009-2019 Volker Theile
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenMediaVault. If not, see <http://www.gnu.org/licenses/>.
 */
class OMVModuleSsh extends \OMV\Engine\Module\ServiceAbstract
  implements \OMV\Engine\Notify\IListener, \OMV\Engine\Module\IServiceStatus,
  \OMV\Engine\Module\IZeroconf {
	/**
	 * Get the module name.
	 */
	public function getName() {
		return "ssh";
	}

	/**
	 * Generate the configuration.
	 */
	public function applyConfig() {
		$cmd = new \OMV\System\Process("omv-mkconf", "ssh");
		$cmd->setRedirect2to1();
		$cmd->execute();
	}

	/**
	 * Stop the managed service.
	 */
	public function stopService() {
		$systemCtl = new \OMV\System\SystemCtl("ssh");
		$systemCtl->disable(TRUE);
	}

	/**
	 * Start the managed service.
	 */
	public function startService() {
		$db = \OMV\Config\Database::getInstance();
		$object = $db->get("conf.service.ssh");
		if (TRUE !== $object->get("enable"))
			return;
		$systemCtl = new \OMV\System\SystemCtl("ssh");
		$systemCtl->enable(TRUE);
	}

	/**
	 * Get the status of the managed service.
	 */
	public function getStatus() {
		$db = \OMV\Config\Database::getInstance();
		$object = $db->get("conf.service.ssh");
		$systemCtl = new \OMV\System\SystemCtl("ssh");
		return [
			"name" => $this->getName(),
			"title" => gettext("SSH"),
			"enabled" => $object->get("enable"),
			"running" => $systemCtl->isActive()
		];
	}

	/**
	 * Get the Zeroconf service configuration.
	 */
	public function getZeroconfConfig() {
		return [
			"id" => "ssh",
			"title" => gettext("SSH")
		];
	}

	/**
	 * Helper function that is called whenever a user has been created,
	 * modified or deleted.
	 * @param type The event message type.
	 * @param path The event message path.
	 * @param object The configuration object.
	 */
	final public function onUser($type, $path, $object) {
		switch ($type) {
		case OMV_NOTIFY_CREATE:
		case OMV_NOTIFY_DELETE:
			// Set the module to dirty when public keys are defined.
			if (!empty($object['sshpubkeys']))
				$this->setDirty();
			break;
		case OMV_NOTIFY_MODIFY:
			// If the 'sshpubkey' is set, then set this module to dirty.
			// Note, it would be better to set the module to dirty only
			// if the 'sshpubkey' field has been modified, but the
			// current framework does not have the capability to do that.
			if (!empty($object['sshpubkeys']))
				$this->setDirty();
			break;
		default:
			// Nothing to do here.
			break;
		}
	}

	/**
	 * Bind listeners.
	 */
	function bindListeners(\OMV\Engine\Notify\Dispatcher $dispatcher) {
		$dispatcher->addListener(
		  OMV_NOTIFY_MODIFY,
		  "org.openmediavault.conf.service.ssh",
		  [ $this, "setDirty" ]);
		$dispatcher->addListener(
		  OMV_NOTIFY_CREATE | OMV_NOTIFY_MODIFY | OMV_NOTIFY_DELETE,
		  "org.openmediavault.conf.system.usermngmnt.user",
		  [ $this, "onUser" ]);
		$moduleMngr = \OMV\Engine\Module\Manager::getInstance();
		$dispatcher->addListener(
		  OMV_NOTIFY_MODIFY,
		  "org.openmediavault.conf.service.ssh",
		  [ $moduleMngr->getModule("zeroconf"), "setDirty" ]);
	}
}
